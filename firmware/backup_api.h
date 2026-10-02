/*
 * backup_api.h - Admin > Settings > Backup & Restore
 * ZXHEIFI - main unit firmware (v2)
 *
 *   GET  /api/admin/backup   (super) - streams one JSON file with everything
 *        this unit knows: settings, prices, vouchers, subscribers, admin
 *        accounts (salted hashes), sales, logs, live sessions and the coin
 *        boxes WITH their keys (so they keep working after a restore -
 *        the Admin warns to keep the file private). Never network.json:
 *        WiFi + MikroTik API credentials come from the new board's Setup
 *        Wizard.
 *   POST /api/admin/restore  (super, multipart field "backup") - the
 *        upload is streamed to /restore.tmp, split per file by
 *        BackupSplitter (backup_split.h) into /r_*.tmp, and only if the
 *        WHOLE backup is valid are they swapped in - from loop(), right
 *        before the restart, so no periodic save can write old data over
 *        them. Works across boards (NodeMCU -> ESP32); pins that don't
 *        exist on this board are reset by AdminAPI::loadSettings().
 * Nothing is held in RAM: both directions stream, so a NodeMCU copes too.
 */
#ifndef BACKUP_API_H
#define BACKUP_API_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include "platform.h"
#include "config.h"
#include "admin_api.h"
#include "session.h"
#include "gui_handler.h"
#include "backup_split.h"

#define RESTORE_UPLOAD_FILE  "/restore.tmp"
#define RESTORE_FLAG_FILE    "/restored.json"
#define RESTORE_MAX_BYTES    1048576UL

static const char* const BACKUP_FILES[] = {
  CONFIG_FILE, VOUCHERS_FILE, SUBSCRIBERS_FILE, ADMIN_ACCOUNTS_FILE, ACTIVITY_LOG_FILE,
  SALES_HISTORY_FILE, TODAY_SALES_FILE, SESSIONS_FILE, VENDOS_FILE, COLLECTIONS_FILE,
};
static const size_t BACKUP_FILE_COUNT = sizeof(BACKUP_FILES) / sizeof(BACKUP_FILES[0]);

class BackupAPI {
public:
  void begin(ZxWebServer& server, GUIHandler& gui, AdminAPI& admin, SessionManager& sessions) {
    _server = &server;
    _gui = &gui;
    _admin = &admin;
    _sessions = &sessions;
    _server->on("/api/admin/backup", HTTP_GET, [this]() { handleBackup(); });
    _server->on("/api/admin/restore", HTTP_POST, [this]() { handleRestoreDone(); },
                [this]() { handleRestoreUpload(); });
    logPreviousRestore();
  }

  // Installs a validated restore and restarts - nothing else runs between
  // the swap and the restart.
  void loop() {
    if (!_installAt || (int32_t)(millis() - _installAt) < 0) return;
    for (auto& name : _ready) {
      String target = "/" + name;
      SPIFFS.remove(target);
      SPIFFS.rename(tempName(name), target);
    }
    File f = SPIFFS.open(RESTORE_FLAG_FILE, "w");
    if (f) {
      f.print("{\"files\":" + String(_ready.size()) + ",\"by\":\"" + _by + "\",\"from\":\"" + _fromBoard + "\"}");
      f.close();
    }
    SPIFFS.remove(RESTORE_UPLOAD_FILE);
    delay(100);
    ESP.restart();
  }

private:
  ZxWebServer* _server = nullptr;
  GUIHandler* _gui = nullptr;
  AdminAPI* _admin = nullptr;
  SessionManager* _sessions = nullptr;

  // upload state
  bool _upAuthorized = false;
  int _upAuthStatus = 0;
  String _upAuthError, _by;
  bool _upSawFile = false;
  String _upError;
  size_t _upSize = 0;
  File _upFile;

  std::vector<String> _ready;     // validated files waiting in /r_<name>.tmp
  String _fromBoard;
  uint32_t _installAt = 0;

  static bool knownFile(const char* name) {
    for (size_t i = 0; i < BACKUP_FILE_COUNT; i++) {
      if (!strcmp(BACKUP_FILES[i] + 1, name)) return true;
    }
    return false;
  }

  static String tempName(const String& name) { return "/r_" + name.substring(0, name.length() - 5) + ".tmp"; }

  void logPreviousRestore() {
    if (!SPIFFS.exists(RESTORE_FLAG_FILE)) return;
    File f = SPIFFS.open(RESTORE_FLAG_FILE, "r");
    String info = f ? f.readString() : String();
    if (f) f.close();
    SPIFFS.remove(RESTORE_FLAG_FILE);
    _admin->logEvent("restore", "backup restored " + info);
  }

  // ---- backup -------------------------------------------------------

  void handleBackup() {
    AdminAccount* a = _gui->authAdmin(true);
    if (!a) return;
    // Make the files current first.
    _sessions->saveToSPIFFS();
    _admin->saveToday();
    _admin->saveActivityLog();

    String date = _admin->todayDateStamp.length() ? _admin->todayDateStamp : String("undated");
    _server->sendHeader("Content-Disposition", "attachment; filename=\"zxheifi-backup-" + date + ".json\"");
    _gui->streamBegin();
    uint32_t now = AdminAPI::clockSynced() ? (uint32_t)time(nullptr) : 0;
    _gui->streamRaw("{\"zxheifiBackup\":1,\"fw\":\"" FIRMWARE_VERSION "\",\"board\":\"" BOARD_NAME
                    "\",\"createdAt\":" + String(now) + ",\"files\":{");
    bool first = true;
    char buf[512];
    for (size_t i = 0; i < BACKUP_FILE_COUNT; i++) {
      const char* path = BACKUP_FILES[i];
      if (!SPIFFS.exists(path) || !looksLikeJson(path)) continue;
      File f = SPIFFS.open(path, "r");
      if (!f) continue;
      _gui->streamRaw(String(first ? "" : ",") + "\"" + (path + 1) + "\":");
      first = false;
      int n;
      while ((n = f.read((uint8_t*)buf, sizeof(buf))) > 0) _gui->streamBytes(buf, (size_t)n);
      f.close();
    }
    _gui->streamRaw("}}");
    _gui->streamEnd();
    _admin->logEvent("backup_downloaded", "by " + a->username);
  }

  // A damaged or empty file is left out rather than breaking the backup.
  static bool looksLikeJson(const char* path) {
    File f = SPIFFS.open(path, "r");
    if (!f) return false;
    int c;
    while ((c = f.read()) >= 0) {
      if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
      f.close();
      return c == '{' || c == '[';
    }
    f.close();
    return false;
  }

  // ---- restore ------------------------------------------------------

  void handleRestoreUpload() {
    HTTPUpload& up = _server->upload();
    if (up.status == UPLOAD_FILE_START) {
      _upSawFile = false;
      _upError = "";
      _upSize = 0;
      AdminAccount* a = _gui->checkAdmin(true, _upAuthStatus, _upAuthError);
      _upAuthorized = a != nullptr;
      _by = a ? a->username : String();
      if (!_upAuthorized || _installAt) return;
      if (up.name != "backup") return;
      _upSawFile = true;
      SPIFFS.remove(RESTORE_UPLOAD_FILE);
      _upFile = SPIFFS.open(RESTORE_UPLOAD_FILE, "w");
      if (!_upFile) _upError = "not_saved_low_memory";
    } else if (up.status == UPLOAD_FILE_WRITE) {
      if (!_upAuthorized || !_upSawFile || _upError.length()) return;
      _upSize += up.currentSize;
      if (_upSize > RESTORE_MAX_BYTES) {
        _upError = "backup_too_large";
        _upFile.close();
        SPIFFS.remove(RESTORE_UPLOAD_FILE);
        return;
      }
      if (_upFile.write(up.buf, up.currentSize) != up.currentSize) _upError = "not_saved_low_memory";
    } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
      if (_upFile) _upFile.close();
      if (up.status == UPLOAD_FILE_ABORTED && !_upError.length()) _upError = "bad_backup";
    }
  }

  struct TempSink : zxb::BackupSplitter::Sink {
    std::vector<String> written;
    File file;
    bool fileStart(const char* name) override {
      if (!knownFile(name)) return false;
      for (auto& w : written) if (w == name) return false;    // the same file twice
      file = SPIFFS.open(tempName(name), "w");
      if (!file) return false;
      written.push_back(name);
      return true;
    }
    void fileData(const char* p, size_t n) override {
      if (file) file.write((const uint8_t*)p, n);
    }
    bool fileEnd() override {
      if (!file) return false;
      file.close();
      return true;
    }
  };

  void cleanupTemps(const std::vector<String>& names) {
    for (auto& n : names) SPIFFS.remove(tempName(n));
    SPIFFS.remove(RESTORE_UPLOAD_FILE);
  }

  void handleRestoreDone() {
    if (!_upAuthorized) {
      _gui->replyError(_upAuthStatus ? _upAuthStatus : 401, _upAuthError.length() ? _upAuthError : "unauthorized");
      return;
    }
    if (_installAt) { _gui->replyError(409, "restore_in_progress"); return; }
    if (_upError.length()) {
      SPIFFS.remove(RESTORE_UPLOAD_FILE);
      _gui->replyError(_upError == "not_saved_low_memory" ? 507 : 400, _upError);
      return;
    }
    if (!_upSawFile || !SPIFFS.exists(RESTORE_UPLOAD_FILE)) { _gui->replyError(400, "no_file"); return; }

    TempSink sink;
    zxb::BackupSplitter splitter(sink);
    File f = SPIFFS.open(RESTORE_UPLOAD_FILE, "r");
    char buf[512];
    int n;
    while (f && (n = f.read((uint8_t*)buf, sizeof(buf))) > 0) {
      splitter.feed(buf, (size_t)n);
      if (splitter.state() != zxb::BackupSplitter::Running) break;
    }
    if (f) f.close();
    if (sink.file) sink.file.close();
    zxb::BackupSplitter::State st = splitter.finish();
    if (st != zxb::BackupSplitter::Ok) {
      cleanupTemps(sink.written);
      _gui->replyError(400, st == zxb::BackupSplitter::NotABackup ? "not_a_backup" : "bad_backup");
      return;
    }
    _ready = sink.written;
    _fromBoard = splitter.board();
    _installAt = millis() + 1500;          // after the reply has gone out
    DynamicJsonDocument out(512);
    out["ok"] = true;
    out["rebootInMs"] = 1500;
    out["fromBoard"] = _fromBoard;
    JsonArray files = out.createNestedArray("files");
    for (auto& name : _ready) files.add(name);
    _gui->replyJson(200, out);
  }
};

#endif // BACKUP_API_H
