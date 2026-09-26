/*
 * gui_handler.h - HTTP/JSON API consumed by mikrotik/gui/script.js
 * ZXHEIFI - NodeMCU ESP8266 Firmware
 *
 * The GUI (login.html / status.html / admin.html) is served by MikroTik's
 * Hotspot HTML directory, not by the NodeMCU — see BUILD_INSTRUCTIONS.md.
 * The NodeMCU only exposes this small JSON API that the GUI's script.js
 * calls over AJAX to redeem vouchers, poll status, and drive the admin
 * dashboard.
 */
#ifndef GUI_HANDLER_H
#define GUI_HANDLER_H

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <sys/time.h>
#include "config.h"
#include "session.h"
#include "admin_api.h"
#include "pppoe.h"
#include "qos.h"
#include "mikrotik_api.h"
#include "telegram.h"
#include "coin_slot.h"
#include "vendo_registry.h"
#include "zx_protocol.h"

class GUIHandler {
public:
  void begin(ESP8266WebServer& server, SessionManager& sessions, AdminAPI& admin,
             PPPoEManager& pppoe, QoSManager& qos, MikrotikAPI& api, TelegramNotifier& telegram) {
    _server = &server;
    _sessions = &sessions;
    _admin = &admin;
    _pppoe = &pppoe;
    _qos = &qos;
    _api = &api;
    _telegram = &telegram;

    // ESP8266WebServer only exposes headers passed here via header();
    // without this, X-Admin-Password would always read back empty.
    // collectHeaders() is a variadic template on this core version - each
    // header name is its own argument, not an array+count pair.
    // X-ZX-Sig: the sub-vendo protocol's signature (vendo_api.h).
    _server->collectHeaders("X-Admin-Username", "X-Admin-Password", "X-Client-Time", ZX_SIG_HEADER);

    _server->on("/api/health", HTTP_GET, [this]() { handleHealth(); });
    _server->on("/api/branding", HTTP_GET, [this]() { handleBranding(); });
    _server->on("/api/login", HTTP_POST, [this]() { handleLogin(); });
    _server->on("/api/status", HTTP_GET, [this]() { handleStatus(); });
    _server->on("/api/extend", HTTP_POST, [this]() { handleExtend(); });
    _server->on("/api/disconnect", HTTP_POST, [this]() { handleDisconnect(); });
    _server->on("/api/pause", HTTP_POST, [this]() { handlePause(); });
    _server->on("/api/resume", HTTP_POST, [this]() { handleResume(); });
    _server->on("/api/coin/start", HTTP_POST, [this]() { handleCoinStart(); });
    _server->on("/api/coin/status", HTTP_GET, [this]() { handleCoinStatus(); });
    _server->on("/api/coin/done", HTTP_POST, [this]() { handleCoinDone(); });
    _server->on("/api/coin/cancel", HTTP_POST, [this]() { handleCoinCancel(); });

    _server->on("/api/admin/overview", HTTP_GET, [this]() { handleAdminOverview(); });
    _server->on("/api/admin/tier-info", HTTP_GET, [this]() { handleAdminTierInfo(); });
    _server->on("/api/admin/users", HTTP_GET, [this]() { handleAdminUsers(); });
    _server->on("/api/admin/settings", HTTP_GET, [this]() { handleAdminGetSettings(); });
    _server->on("/api/admin/settings", HTTP_POST, [this]() { handleAdminSaveSettings(); });
    _server->on("/api/admin/rate-profiles", HTTP_GET, [this]() { handleAdminGetRateProfiles(); });
    _server->on("/api/admin/rate-profiles", HTTP_POST, [this]() { handleAdminSaveRateProfiles(); });
    _server->on("/api/admin/kick", HTTP_POST, [this]() { handleAdminKick(); });
    _server->on("/api/admin/block", HTTP_POST, [this]() { handleAdminBlock(); });
    _server->on("/api/admin/unblock", HTTP_POST, [this]() { handleAdminUnblock(); });
    _server->on("/api/admin/blocked", HTTP_GET, [this]() { handleAdminListBlocked(); });
    _server->on("/api/admin/vouchers/import", HTTP_POST, [this]() { handleAdminImportVouchers(); });
    _server->on("/api/admin/vouchers/generate", HTTP_POST, [this]() { handleAdminGenerateVouchers(); });
    _server->on("/api/admin/subscribers", HTTP_GET, [this]() { handleAdminSubscribersList(); });
    _server->on("/api/admin/subscribers", HTTP_POST, [this]() { handleAdminAddSubscriber(); });
    _server->on("/api/admin/subscribers/renew", HTTP_POST, [this]() { handleAdminRenewSubscriber(); });
    _server->on("/api/admin/subscribers/toggle", HTTP_POST, [this]() { handleAdminToggleSubscriber(); });
    _server->on("/api/admin/accounts", HTTP_GET, [this]() { handleAdminAccountsList(); });
    _server->on("/api/admin/accounts", HTTP_POST, [this]() { handleAdminAddAccount(); });
    _server->on("/api/admin/accounts/toggle", HTTP_POST, [this]() { handleAdminToggleAccount(); });
    _server->on("/api/admin/logs", HTTP_GET, [this]() { handleAdminLogs(); });
    _server->on("/api/admin/sales", HTTP_GET, [this]() { handleAdminSales(); });

    // This GUI is normally served BY the MikroTik router, a different
    // origin from the NodeMCU that actually implements this API (see
    // script.js's apiBase()) - every real request above is therefore
    // cross-origin in production, which means the browser sends a CORS
    // preflight (OPTIONS) before it before the actual GET/POST. No path
    // above is registered for HTTP_OPTIONS, so ESP8266WebServer falls
    // through to onNotFound() for every one of them - handle that here
    // generically instead of registering an OPTIONS variant per route.
    _server->onNotFound([this]() {
      if (_server->method() == HTTP_OPTIONS) {
        setCorsHeaders();
        _server->send(204);
      } else {
        sendError(404, "not_found");
      }
    });
  }

  void setMikrotikReachable(bool reachable) { _mikrotikReachable = reachable; }
  void setCoinSlot(CoinSlot& coin) { _coin = &coin; }
  void setVendoRegistry(VendoRegistry& vendos) { _vendos = &vendos; }

  // Shared with vendo_api.h, whose admin endpoints use the same auth,
  // CORS and JSON conventions as everything here.
  void replyJson(int code, const JsonDocument& doc) { sendJson(code, doc); }
  void replyError(int code, const String& message) { sendError(code, message); }
  void replyOk() { sendOk(); }
  AdminAccount* authAdmin(bool requireSuper = false) { return requireAdmin(requireSuper); }
  void streamBegin() { beginStream(); }
  void streamJsonItem(const JsonDocument& item, bool& first) { streamItem(item, first); }
  void streamRaw(const String& text) { _server->sendContent(text); }
  void streamEnd() { endStream(); }
  // Set by the .ino - re-attaches the coin interrupt/relay after Settings
  // > Coin Slot changes (they live in the sketch, not here).
  void (*onCoinPinsChanged)() = nullptr;

private:
  ESP8266WebServer* _server = nullptr;
  SessionManager* _sessions = nullptr;
  AdminAPI* _admin = nullptr;
  PPPoEManager* _pppoe = nullptr;
  QoSManager* _qos = nullptr;
  MikrotikAPI* _api = nullptr;
  TelegramNotifier* _telegram = nullptr;
  CoinSlot* _coin = nullptr;
  VendoRegistry* _vendos = nullptr;
  bool _mikrotikReachable = false;

  // ---- Brute-force protection (shared by /api/login and admin auth) -----
  // Small fixed-size per-IP table - nothing here persists across a
  // reboot, which is fine for its purpose (slowing down an active
  // guessing script, not a permanent ban list).
  struct LoginAttempt {
    uint32_t ip = 0;
    uint8_t failCount = 0;
    uint32_t lastFailMs = 0;
  };
  LoginAttempt _attempts[LOGIN_ATTEMPT_TABLE_SIZE];

  uint32_t clientIp() { return (uint32_t)_server->client().remoteIP(); }

  LoginAttempt* findAttempt(uint32_t ip) {
    for (auto& a : _attempts) {
      if (a.ip == ip) return &a;
    }
    return nullptr;
  }

  // Finds the existing entry for this IP, or claims an empty/oldest slot
  // for it. The table is small on purpose (ESP8266 RAM) - once full, the
  // least-recently-failed IP is evicted first, since it's the one least
  // likely to be an ongoing attack.
  LoginAttempt* claimAttemptSlot(uint32_t ip) {
    LoginAttempt* existing = findAttempt(ip);
    if (existing) return existing;
    LoginAttempt* victim = &_attempts[0];
    for (auto& a : _attempts) {
      if (a.ip == 0) { victim = &a; break; }
      if (a.lastFailMs < victim->lastFailMs) victim = &a;
    }
    victim->ip = ip;
    victim->failCount = 0;
    victim->lastFailMs = 0;
    return victim;
  }

  // Checked BEFORE verifying credentials, so a locked-out client can't
  // keep guessing while waiting for the lockout to be evaluated. A
  // lockout self-clears once LOGIN_LOCKOUT_MS has passed since the last
  // failure, rather than needing an explicit unlock step.
  bool isLockedOut(uint32_t ip) {
    LoginAttempt* a = findAttempt(ip);
    if (!a || a->failCount < LOGIN_MAX_FAILURES) return false;
    if (millis() - a->lastFailMs > LOGIN_LOCKOUT_MS) {
      a->failCount = 0;
      return false;
    }
    return true;
  }

  void recordLoginFailure(uint32_t ip) {
    LoginAttempt* a = claimAttemptSlot(ip);
    if (a->failCount < 255) a->failCount++;
    a->lastFailMs = millis();
  }

  void recordLoginSuccess(uint32_t ip) {
    LoginAttempt* a = findAttempt(ip);
    if (a) a->failCount = 0;
  }

  // Allows any origin - this is a local captive-portal API with no
  // cookies/session state to steal cross-site (admin auth is a custom
  // header the calling page must set explicitly, which a malicious
  // third-party page can't forge through a plain cross-origin request
  // anyway); the real reason CORS is needed at all is that the GUI's
  // own origin (MikroTik) and this API's origin (NodeMCU) are two
  // different devices by design, not a security boundary being relaxed.
  // Must be called before any send()/sendHeader() response is written.
  void setCorsHeaders() {
    _server->sendHeader("Access-Control-Allow-Origin", "*");
    _server->sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    _server->sendHeader("Access-Control-Allow-Headers", "Content-Type, X-Admin-Username, X-Admin-Password, X-Client-Time");
  }

  // Serializes straight into the socket instead of into a String first -
  // on an ESP8266 with ~40KB free heap, holding the document AND a full
  // copy of its text at once was enough to fail on the bigger replies.
  void sendJson(int code, const JsonDocument& doc) {
    setCorsHeaders();
    _server->setContentLength(measureJson(doc));
    _server->send(code, "application/json", "");
    WiFiClient client = _server->client();
    serializeJson(doc, client);
  }

  // Was `setCorsHeaders(); sendOk();` - an infinite recursion that crashed
  // (and rebooted) the NodeMCU on disconnect/pause/resume and most admin
  // actions. The mock server never shared this code, so tests stayed green.
  void sendOk() {
    setCorsHeaders();
    _server->send(200, "application/json", "{\"ok\":true}");
  }

  // Chunked streaming for list endpoints whose size grows with the data
  // (logs, sales, subscribers, users): one small document per item, so
  // memory stays flat no matter how many there are. The old fixed-size
  // documents silently dropped items past their capacity.
  void beginStream() {
    setCorsHeaders();
    _server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server->send(200, "application/json", "");
  }
  void streamItem(const JsonDocument& item, bool& first) {
    String s;
    serializeJson(item, s);
    if (!first) s = "," + s;
    first = false;
    _server->sendContent(s);
  }
  void endStream() { _server->sendContent(""); }

  void sendError(int code, const String& message) {
    DynamicJsonDocument doc(128);
    doc["error"] = message;
    sendJson(code, doc);
  }

  // Without internet the NodeMCU never gets NTP time, and anything dated
  // (subscriber expiry, voucher validity, the night promo, daily sales)
  // can't work. An authenticated admin's browser sends its own clock in
  // X-Client-Time; it's used only until NTP (which keeps running) takes
  // over, and only from a logged-in admin, so a customer can't move the
  // clock to stretch their time.
  void adoptBrowserClock() {
    if (AdminAPI::clockSynced()) return;
    uint32_t t = strtoul(_server->header("X-Client-Time").c_str(), nullptr, 10);
    if (t < 1700000000UL || t > 4000000000UL) return;
    timeval tv = { (time_t)t, 0 };
    settimeofday(&tv, nullptr);
    _admin->logEvent("clock_set", "from the admin's browser (no internet time yet)");
  }

  // Returns the authenticated AdminAccount, or sends 401/403/429 and
  // returns nullptr. Pass requireSuper=true for settings/subscriptions/
  // account management - anything with billing or system-wide
  // consequences.
  AdminAccount* requireAdmin(bool requireSuper = false) {
    uint32_t ip = clientIp();
    if (isLockedOut(ip)) {
      sendError(429, "too_many_attempts");
      return nullptr;
    }
    String user = _server->header("X-Admin-Username");
    String pass = _server->header("X-Admin-Password");
    AdminAccount* a = _admin->checkAdminLogin(user, pass);
    if (!a) {
      recordLoginFailure(ip);
      sendError(401, "unauthorized");
      return nullptr;
    }
    recordLoginSuccess(ip);
    adoptBrowserClock();
    if (requireSuper && a->role != "super") {
      sendError(403, "super_admin_required");
      return nullptr;
    }
    return a;
  }

  // ---- Public endpoints -----------------------------------------------

  void handleHealth() {
    DynamicJsonDocument doc(192);
    doc["mikrotik"] = _mikrotikReachable;
    doc["uptimeMs"] = millis();
    doc["fw"] = FIRMWARE_VERSION;
    doc["board"] = BOARD_NAME;
    doc["freeHeap"] = ESP.getFreeHeap();
    sendJson(200, doc);
  }

  // No auth - the login/status pages need this before any admin session
  // exists, to show the shop's name/color.
  // Also carries the announcement and the price list for the portal
  // (Settings > Announcement / Rate Profiles) - all public information.
  void handleBranding() {
    DynamicJsonDocument out(3072);
    out["brandName"] = _admin->brandName;
    out["brandColor"] = _admin->brandColor;
    out["soundEnabled"] = _admin->soundEnabled;
    out["announcement"] = _admin->announcement;
    JsonArray rates = out.createNestedArray("rates");
    for (auto& r : _admin->rateProfiles) {
      JsonObject o = rates.createNestedObject();
      o["peso"] = r.pesoAmount;
      o["minutes"] = r.minutes;
      o["dataMb"] = r.dataMb;
      o["validityMinutes"] = r.validityMinutes;
    }
    // Coin boxes the customer can pick (Main + paired sub vendos).
    JsonArray vs = out.createNestedArray("vendos");
    if (_vendos) {
      for (auto& v : _vendos->all()) {
        if (v.id && !v.hasKey) continue;
        JsonObject o = vs.createNestedObject();
        o["id"] = v.id;
        o["name"] = v.name;
        o["online"] = _vendos->isOnline(v);
      }
    }
    sendJson(200, out);
  }

  void handleLogin() {
    uint32_t ip = clientIp();
    if (isLockedOut(ip)) {
      sendError(429, "too_many_attempts");
      return;
    }
    DynamicJsonDocument in(256);
    if (deserializeJson(in, _server->arg("plain"))) {
      sendError(400, "bad_json");
      return;
    }
    String code = in["code"] | "";
    String password = in["password"] | "";
    String mode = in["mode"] | "hotspot";
    code.trim();
    if (!code.length() || (mode != "hotspot" && mode != "pppoe")) {
      sendError(400, "missing_code_or_mode");
      return;
    }
    // Backstop for the RouterOS ip-binding block (which normally stops a
    // blocked phone before it ever sees this page).
    if (_admin->isMacBlocked(in["mac"] | "")) {
      sendError(403, "device_blocked");
      return;
    }

    // A username+password pair only makes sense as a subscriber login,
    // so subscribers are checked first; vouchers don't have passwords.
    // A subscriber check failing here isn't counted as a login failure -
    // it's the normal case for every plain voucher code, not a wrong
    // guess. Only a code that fails BOTH checks (below) is a real
    // failed attempt.
    Subscriber* sub = _admin->checkSubscriberLogin(code, password);
    if (sub) {
      // Kick any stale active MikroTik session first - a phone that
      // randomizes its MAC address per-reconnect (common on modern
      // Android/iOS) re-triggers the hotspot walled garden under a NEW
      // MAC, but the OLD MAC's active session is never cleaned up by
      // provision() alone (see kickActiveHotspotUser()'s comment) and
      // would otherwise block the new MAC's login under the profile's
      // shared-users=1. Harmless no-op on a genuinely fresh login.
      if (mode == "hotspot") _api->kickActiveHotspotUser(code);
      // Pass the subscriber's actual (already-verified) password
      // through, so the MikroTik account created for them matches what
      // they'll actually authenticate with (their PPPoE dialer, or the
      // hotspot walled-garden login the GUI submits next) - defaulting
      // to the session code here, like vouchers do, would silently set
      // their real login to something they never typed.
      if (!_pppoe->provision(*_api, *_qos, code, mode, sub->tier, UNLIMITED_SECONDS, UNLIMITED_BYTES, password)) {
        sendError(502, "mikrotik_unreachable");
        return;
      }
      if (!_sessions->addSession(code, mode, sub->tier, UNLIMITED_SECONDS, UNLIMITED_BYTES, /*isSubscriber=*/true)) {
        _pppoe->deprovision(*_api, code, mode);
        sendError(503, "too_many_users_try_later");
        return;
      }
      _admin->logEvent("subscriber_login", code + " (" + mode + ")");
      _telegram->queueMessage("Subscriber login: " + code + " (" + mode + ", Tier " + sub->tier + ")");
      recordLoginSuccess(ip);

      DynamicJsonDocument out(256);
      out["sessionId"] = code;
      out["mode"] = mode;
      out["tier"] = sub->tier;
      out["timeRemainingSec"] = UNLIMITED_SECONDS;
      out["dataRemainingBytes"] = (double)UNLIMITED_BYTES;
      out["nightPromoApplied"] = false;
      out["isSubscriber"] = true;
      sendJson(200, out);
      return;
    }

    // Reconnect: a device resubmitting a code that already has an
    // ACTIVE NodeMCU-tracked session - most commonly the same random-MAC
    // scenario as above, where AdminAPI already marked the voucher used
    // from the FIRST (still-running) login and would otherwise reject
    // this as "invalid_or_used_voucher" even though the session is
    // legitimately still theirs. Resumes with the CURRENT remaining
    // budget rather than re-redeeming (which would double-charge the
    // tier's full time/data on top of what's already been used).
    // Voucher codes are printed upper-case; phones often auto-capitalise
    // only the first letter, so match regardless of case from here on.
    code.toUpperCase();
    SessionEntry* existing = _sessions->findSession(code);
    if (existing && !existing->isSubscriber) {
      if (existing->paused) _sessions->resumeSession(code);
      if (existing->mode == "hotspot") _api->kickActiveHotspotUser(code);
      if (!_pppoe->provision(*_api, *_qos, code, existing->mode, existing->tier,
                              existing->remainingSeconds, existing->remainingBytes)) {
        sendError(502, "mikrotik_unreachable");
        return;
      }
      _admin->logEvent("voucher_reconnect", code + " (" + existing->mode + ")");
      recordLoginSuccess(ip);

      DynamicJsonDocument out(256);
      out["sessionId"] = code;
      out["mode"] = existing->mode;
      out["tier"] = existing->tier;
      out["timeRemainingSec"] = existing->remainingSeconds;
      out["dataRemainingBytes"] = (double)existing->remainingBytes;
      out["nightPromoApplied"] = false;
      out["isSubscriber"] = false;
      sendJson(200, out);
      return;
    }

    VoucherRecord record;
    if (!_admin->redeemVoucher(code, record)) {
      recordLoginFailure(ip);
      sendError(400, "invalid_or_used_voucher");
      return;
    }
    if (mode == "hotspot") _api->kickActiveHotspotUser(code); // defensive - no-op on a genuinely fresh code

    uint64_t dataBytes = record.dataBytes;
    uint32_t timeSeconds = record.timeSeconds;
    bool nightPromoApplied = false;
    // dataBytes is already UNLIMITED_BYTES for a time-only Rate Profile
    // (dataMb=0, see AdminAPI::generateVouchers()) - multiplying that
    // sentinel would overflow into a bogus finite value, so it's left
    // alone; "unlimited" already implies the night promo's data bonus.
    // (The old dedicated "night" tier flag is gone - a discounted
    // night-only product is now just another Rate Profile the admin
    // defines, like any other peso amount; this automatic time-of-day
    // bonus applies uniformly on top of whichever profile was bought.)
    if (dataBytes != UNLIMITED_BYTES &&
        _admin->settingNightPromoEnabled && _qos->isNightPromoActive()) {
      dataBytes *= NIGHT_PROMO_DATA_MULT;
      nightPromoApplied = true;
    }

    if (!_pppoe->provision(*_api, *_qos, code, mode, record.tier, timeSeconds, dataBytes)) {
      _admin->rollbackVoucher(code); // so the code can be retried, and the sale isn't counted
      sendError(502, "mikrotik_unreachable");
      return;
    }

    if (!_sessions->addSession(code, mode, record.tier, timeSeconds, dataBytes,
                                /*isSubscriber=*/false, record.pauseWindowMinutes)) {
      // Session table full: an untracked session would never be metered
      // or cut off, so undo the provision rather than give free access.
      _pppoe->deprovision(*_api, code, mode);
      _admin->rollbackVoucher(code);
      sendError(503, "too_many_users_try_later");
      return;
    }
    _admin->forgetUsedVoucher(code);
    _admin->logEvent("voucher_login", code + " tier " + record.tier + " (" + mode + ")");
    _telegram->queueMessage("Voucher sale: " + code + " - PHP " + String(record.price, 2) +
                             " (Tier " + record.tier + ", " + mode + ")");
    recordLoginSuccess(ip);

    DynamicJsonDocument out(256);
    out["sessionId"] = code;
    out["mode"] = mode;
    out["tier"] = record.tier;
    out["timeRemainingSec"] = timeSeconds;
    out["dataRemainingBytes"] = (double)dataBytes;
    out["nightPromoApplied"] = nightPromoApplied;
    out["isSubscriber"] = false;
    sendJson(200, out);
  }

  void handleStatus() {
    String sessionId = _server->arg("session");
    SessionEntry* e = _sessions->findSession(sessionId);
    if (!e) {
      sendError(404, "session_not_found");
      return;
    }
    DynamicJsonDocument out(256);
    out["sessionId"] = e->sessionId;
    out["mode"] = e->mode;
    out["tier"] = e->tier;
    out["timeRemainingSec"] = e->remainingSeconds;
    out["dataRemainingBytes"] = (double)e->remainingBytes;
    out["idle"] = _sessions->isIdle(sessionId);
    out["paused"] = e->paused;
    out["isSubscriber"] = e->isSubscriber;
    sendJson(200, out);
  }

  void handleExtend() {
    DynamicJsonDocument in(256);
    if (deserializeJson(in, _server->arg("plain"))) { sendError(400, "bad_json"); return; }
    String sessionId = in["session"] | "";
    String code = in["code"] | "";

    SessionEntry* existing = _sessions->findSession(sessionId);
    if (!existing) { sendError(404, "session_not_found"); return; }
    if (existing->isSubscriber) { sendError(400, "subscriptions_dont_extend"); return; }

    code.trim();
    code.toUpperCase();
    VoucherRecord record;
    if (!_admin->redeemVoucher(code, record)) { sendError(400, "invalid_or_used_voucher"); return; }

    // Re-provision with the combined totals (delete+recreate on MikroTik)
    // BEFORE crediting, so a failed provision can hand the voucher back
    // instead of eating it.
    uint32_t newSeconds = existing->remainingSeconds + record.timeSeconds;
    uint64_t newBytes = (existing->remainingBytes == UNLIMITED_BYTES || record.dataBytes == UNLIMITED_BYTES)
                            ? UNLIMITED_BYTES : existing->remainingBytes + record.dataBytes;
    if (!_pppoe->provision(*_api, *_qos, sessionId, existing->mode, existing->tier, newSeconds, newBytes)) {
      _admin->rollbackVoucher(code);
      sendError(502, "mikrotik_unreachable");
      return;
    }
    existing->remainingSeconds = newSeconds;
    existing->remainingBytes = newBytes;
    existing->lastBytesSeen = 0; // the re-created MikroTik account starts its byte counters at 0
    SessionEntry* updated = existing;
    _admin->forgetUsedVoucher(code);
    _admin->logEvent("extend", sessionId + " +" + code);

    DynamicJsonDocument out(256);
    out["sessionId"] = sessionId;
    out["timeRemainingSec"] = updated->remainingSeconds;
    out["dataRemainingBytes"] = (double)updated->remainingBytes;
    sendJson(200, out);
  }

  // ---- Coin sessions (see coin_slot.h) --------------------------------
  // No admin auth by design - these are the customer's own login-page
  // buttons. The random token returned by start is what ties the coins
  // to this browser; the MAC is only recorded for the activity log.

  void handleCoinStart() {
    DynamicJsonDocument in(256);
    if (deserializeJson(in, _server->arg("plain"))) { sendError(400, "bad_json"); return; }
    if (_admin->isMacBlocked(in["mac"] | "")) { sendError(403, "device_blocked"); return; }
    String token = in["token"] | "";
    int vendo = in["vendo"] | 0;
    if (vendo < 0 || vendo > MAX_SUB_VENDOS) { sendError(404, "vendo_unknown"); return; }
    uint32_t waitSec = 0;
    String err = _coin->start((uint8_t)vendo, in["mac"] | "", in["session"] | "", token, waitSec);
    DynamicJsonDocument out(128);
    if (err == "vendo_unknown") { sendError(404, err); return; }
    if (err.length()) {
      out["error"] = err;
      if (err == "coin_slot_busy") out["waitSec"] = waitSec;
      sendJson(409, out);
      return;
    }
    out["token"] = token;
    out["timeoutSec"] = COIN_NO_COIN_TIMEOUT_MS / 1000;
    out["vendo"] = vendo;
    sendJson(200, out);
  }

  void handleCoinStatus() {
    DynamicJsonDocument out(256);
    _coin->status(_server->arg("token"), out);
    sendJson(200, out);
  }

  void handleCoinDone() {
    DynamicJsonDocument in(128);
    if (deserializeJson(in, _server->arg("plain"))) { sendError(400, "bad_json"); return; }
    String code;
    bool extended = false;
    String err = _coin->done(in["token"] | "", code, extended);
    if (err.length()) {
      sendError(err == "mikrotik_unreachable" ? 502 : err == "too_many_users_try_later" ? 503 : 400, err);
      return;
    }
    SessionEntry* e = _sessions->findSession(code);
    DynamicJsonDocument out(256);
    out["sessionId"] = code;
    out["mode"] = "hotspot";
    out["extended"] = extended;
    out["timeRemainingSec"] = e ? e->remainingSeconds : 0;
    out["dataRemainingBytes"] = e ? (double)e->remainingBytes : 0;
    sendJson(200, out);
  }

  void handleCoinCancel() {
    DynamicJsonDocument in(128);
    if (deserializeJson(in, _server->arg("plain"))) { sendError(400, "bad_json"); return; }
    _coin->cancel(in["token"] | "");
    sendOk();
  }

  void handleDisconnect() {
    DynamicJsonDocument in(128);
    if (deserializeJson(in, _server->arg("plain"))) { sendError(400, "bad_json"); return; }
    String sessionId = in["session"] | "";
    SessionEntry* e = _sessions->findSession(sessionId);
    if (!e) { sendError(404, "session_not_found"); return; }

    _pppoe->deprovision(*_api, sessionId, e->mode);
    _sessions->removeSession(sessionId);
    _admin->logEvent("disconnect", sessionId);
    sendOk();
  }

  void handlePause() {
    DynamicJsonDocument in(128);
    if (deserializeJson(in, _server->arg("plain"))) { sendError(400, "bad_json"); return; }
    String sessionId = in["session"] | "";
    SessionEntry* e = _sessions->findSession(sessionId);
    if (!e) { sendError(404, "session_not_found"); return; }
    if (e->paused) { sendError(400, "already_paused"); return; }

    // Actually cut access on MikroTik - a "pause" that leaves the user
    // connected would just be free unmetered internet.
    _pppoe->deprovision(*_api, sessionId, e->mode);
    _sessions->pauseSession(sessionId);
    sendOk();
  }

  void handleResume() {
    DynamicJsonDocument in(256);
    if (deserializeJson(in, _server->arg("plain"))) { sendError(400, "bad_json"); return; }
    String sessionId = in["session"] | "";
    // Pause fully deprovisions on MikroTik (see handlePause), so Resume
    // has to recreate the account - a subscriber's real password isn't
    // stored in plaintext (only its hash, in AdminAPI), so the client
    // resends whatever it cached from the original login (see script.js,
    // empty for vouchers - they fall back to the session code, same as
    // at login time).
    String password = in["password"] | "";
    SessionEntry* e = _sessions->findSession(sessionId);
    if (!e) { sendError(404, "session_not_found"); return; }
    if (!e->paused) { sendError(400, "not_paused"); return; }

    if (!_pppoe->provision(*_api, *_qos, sessionId, e->mode, e->tier, e->remainingSeconds, e->remainingBytes, password)) {
      sendError(502, "mikrotik_unreachable");
      return;
    }
    _sessions->resumeSession(sessionId);
    sendOk();
  }

  // ---- Admin endpoints --------------------------------------------------

  // Deliberately separate from handleDisconnect() (the customer's own
  // self-service path, unauthenticated by design - same trust level as
  // login) - that endpoint has no admin/ownership check at all, so the
  // Active Users "Kick" button used to call it directly with nothing
  // proving the request was actually admin-authorized. This one is.
  void handleAdminKick() {
    AdminAccount* admin = requireAdmin();
    if (!admin) return;
    DynamicJsonDocument in(128);
    if (deserializeJson(in, _server->arg("plain"))) { sendError(400, "bad_json"); return; }
    String sessionId = in["session"] | "";
    SessionEntry* e = _sessions->findSession(sessionId);
    if (!e) { sendError(404, "session_not_found"); return; }

    _pppoe->deprovision(*_api, sessionId, e->mode);
    _sessions->removeSession(sessionId);
    _admin->logEvent("admin_kick", sessionId + " by " + admin->username);
    sendOk();
  }

  // Kicks the session AND blocks its MAC at the RouterOS layer (an
  // ip-binding type=blocked entry) so the same device can't just log
  // back in with a fresh code - enforcement lives on the router, not
  // the NodeMCU, so it holds even if the NodeMCU reboots.
  void handleAdminBlock() {
    AdminAccount* admin = requireAdmin();
    if (!admin) return;
    DynamicJsonDocument in(128);
    if (deserializeJson(in, _server->arg("plain"))) { sendError(400, "bad_json"); return; }
    String sessionId = in["session"] | "";
    SessionEntry* e = _sessions->findSession(sessionId);
    if (!e) { sendError(404, "session_not_found"); return; }

    String mac = _api->activeSessionMac(sessionId);
    _pppoe->deprovision(*_api, sessionId, e->mode);
    _sessions->removeSession(sessionId);
    bool blocked = false;
    if (mac.length()) {
      blocked = _api->blockMac(mac);
      _admin->addBlockedMac(mac);
    }
    _admin->logEvent("admin_block", sessionId + " (" + mac + ") by " + admin->username);

    DynamicJsonDocument out(128);
    out["ok"] = true;
    out["mac"] = mac;
    out["mikrotikBlocked"] = blocked;
    sendJson(200, out);
  }

  void handleAdminUnblock() {
    AdminAccount* admin = requireAdmin();
    if (!admin) return;
    DynamicJsonDocument in(96);
    if (deserializeJson(in, _server->arg("plain"))) { sendError(400, "bad_json"); return; }
    String mac = in["mac"] | "";
    if (!mac.length()) { sendError(400, "missing_mac"); return; }

    bool unblocked = _api->unblockMac(mac);
    _admin->removeBlockedMac(mac);
    _admin->logEvent("admin_unblock", mac + " by " + admin->username);

    DynamicJsonDocument out(64);
    out["ok"] = true;
    out["mikrotikUnblocked"] = unblocked;
    sendJson(200, out);
  }

  void handleAdminListBlocked() {
    if (!requireAdmin()) return;
    DynamicJsonDocument out(1024);
    JsonArray arr = out.to<JsonArray>();
    for (auto& m : _admin->blockedMacs) arr.add(m);
    sendJson(200, out);
  }

  void handleAdminOverview() {
    AdminAccount* admin = requireAdmin();
    if (!admin) return;
    uint16_t active = 0;
    for (uint8_t i = 0; i < _sessions->count(); i++) {
      if (_sessions->entryAt(i).active) active++;
    }
    DynamicJsonDocument out(384);
    out["totalUsers"] = _admin->usersToday;
    out["activeSessions"] = active;
    out["dataUsedTodayBytes"] = (double)_admin->dataUsedTodayBytes;
    out["revenueToday"] = _admin->revenueToday();
    out["coinRevenueToday"] = _admin->coinRevenueToday;
    out["voucherRevenueToday"] = _admin->voucherRevenueToday;
    out["subscriptionRevenueToday"] = _admin->subscriptionRevenueToday;
    out["unusedVouchers"] = _admin->unusedVoucherCount();
    out["mikrotik"] = _mikrotikReachable;
    out["role"] = admin->role; // lets the GUI show/hide super-only tabs
    sendJson(200, out);
  }

  // Read-only, display-only rate profiles - deliberately open to BOTH
  // roles (unlike handleAdminGetSettings, which is super-only and
  // returns the full editable Settings). Staff can generate vouchers
  // and needs to see current pricing on the dropdown, but shouldn't get
  // the rest of Settings (branding, night promo, etc).
  void handleAdminTierInfo() {
    if (!requireAdmin()) return;
    sendRateProfiles();
  }

  void sendRateProfiles() {
    DynamicJsonDocument out(2048);
    JsonArray arr = out.to<JsonArray>();
    for (auto& p : _admin->rateProfiles) {
      JsonObject o = arr.createNestedObject();
      o["pesoAmount"] = p.pesoAmount;
      o["minutes"] = p.minutes;
      o["dataMb"] = p.dataMb;
      o["validityMinutes"] = p.validityMinutes;
      o["speedProfile"] = p.speedProfile;
    }
    sendJson(200, out);
  }

  // GET/export - same shape as sendRateProfiles(), used by the admin
  // dashboard's "Export Rates" download and by vouchers/generator.py's
  // shared rate_profiles.json (kept super-only since export doubles as
  // "download my current pricing", arguably sensitive).
  void handleAdminGetRateProfiles() {
    if (!requireAdmin(/*requireSuper=*/true)) return;
    sendRateProfiles();
  }

  // POST/import - replaces the ENTIRE rate profile list (not a merge),
  // matching "Import Rates" in the dashboard: admin picks a JSON file
  // (their own earlier export, or one shared from another deployment)
  // and it becomes the new live table. Existing vouchers/sessions keep
  // whatever was baked in at their own generation/provision time either
  // way (see VoucherRecord/SessionEntry) - importing new rates never
  // retroactively changes something already sold.
  // Returns "" or an error code. `speeds` = the speed profiles the rates
  // must point at (the saved ones, or the ones arriving in the same save).
  String parseRateProfiles(JsonArray arr, const std::vector<SpeedProfile>& speeds,
                           std::vector<RateProfile>& out) {
    if (arr.size() > MAX_RATE_PROFILES) return "too_many_profiles";
    for (JsonObject o : arr) {
      RateProfile p;
      p.pesoAmount = o["pesoAmount"] | 0;
      p.minutes = o["minutes"] | 0;
      p.dataMb = o["dataMb"] | 0;
      p.validityMinutes = o["validityMinutes"] | 0;
      p.speedProfile = o["speedProfile"] | String("1");
      if (p.pesoAmount == 0 || p.minutes == 0) return "invalid_profile";
      bool known = false;
      for (auto& sp : speeds) if (sp.id == p.speedProfile) known = true;
      if (!known) return "unknown_speed_profile";
      // Two profiles for the same peso amount: a coin could only ever
      // match the first one, so the second would be silently ignored.
      for (auto& other : out) {
        if (other.pesoAmount == p.pesoAmount) return "duplicate_peso_amount";
      }
      out.push_back(p);
    }
    return "";
  }

  void handleAdminSaveRateProfiles() {
    AdminAccount* admin = requireAdmin(/*requireSuper=*/true);
    if (!admin) return;
    DynamicJsonDocument in(4096);
    if (deserializeJson(in, _server->arg("plain"))) { sendError(400, "bad_json"); return; }
    if (!in.is<JsonArray>()) { sendError(400, "expected_array"); return; }

    std::vector<RateProfile> newProfiles;
    String err = parseRateProfiles(in.as<JsonArray>(), _admin->speedProfiles, newProfiles);
    if (err.length()) { sendError(400, err); return; }
    in.clear();
    in.shrinkToFit();
    std::vector<RateProfile> previous = _admin->rateProfiles;
    _admin->rateProfiles = newProfiles;
    if (!_admin->saveSettings()) {
      _admin->rateProfiles = previous;
      sendError(507, "not_saved_low_memory");
      return;
    }
    _admin->logEvent("rate_profiles_changed", "by " + admin->username + " (" + String(newProfiles.size()) + " profiles)");
    sendOk();
  }

  void handleAdminUsers() {
    if (!requireAdmin()) return;
    beginStream();
    _server->sendContent("[");
    bool first = true;
    for (uint8_t i = 0; i < _sessions->count(); i++) {
      SessionEntry& e = _sessions->entryAt(i);
      if (!e.active) continue;
      StaticJsonDocument<320> o;
      o["sessionId"] = e.sessionId;
      o["mode"] = e.mode;
      o["tier"] = e.tier;
      o["timeRemainingSec"] = e.remainingSeconds;
      o["dataRemainingBytes"] = (double)e.remainingBytes;
      o["paused"] = e.paused;
      o["isSubscriber"] = e.isSubscriber;
      streamItem(o, first);
    }
    _server->sendContent("]");
    endStream();
  }

  void handleAdminGetSettings() {
    AdminAccount* admin = requireAdmin(/*requireSuper=*/true);
    if (!admin) return;
    DynamicJsonDocument out(3072);
    out["nightPromoEnabled"] = _admin->settingNightPromoEnabled;
    out["idleTimeoutMin"] = _admin->settingIdleTimeoutMin;
    out["autoRebootTime"] = _admin->settingAutoRebootTime;
    out["brandName"] = _admin->brandName;
    out["brandColor"] = _admin->brandColor;
    out["soundEnabled"] = _admin->soundEnabled;
    JsonArray speeds = out.createNestedArray("speedProfiles");
    for (auto& sp : _admin->speedProfiles) {
      JsonObject o = speeds.createNestedObject();
      o["id"] = sp.id;
      o["name"] = sp.name;
      o["downMbps"] = sp.downMbps;
      o["upMbps"] = sp.upMbps;
    }
    out["announcement"] = _admin->announcement;
    out["coinPin"] = _admin->coinPin;
    out["relayPin"] = _admin->relayPin;
    out["relayActiveHigh"] = _admin->relayActiveHigh;
    out["coinPulseValue"] = _admin->coinPulseValue;
    out["telegramEnabled"] = _admin->telegramEnabled;
    out["telegramBotToken"] = _admin->telegramBotToken;
    out["telegramChatId"] = _admin->telegramChatId;
    sendJson(200, out);
  }

  void handleAdminSaveSettings() {
    AdminAccount* admin = requireAdmin(/*requireSuper=*/true);
    if (!admin) return;
    DynamicJsonDocument in(6144);
    if (deserializeJson(in, _server->arg("plain"))) { sendError(400, "bad_json"); return; }

    // Validate everything before changing anything. Speed and rate
    // profiles arrive together so a new speed can be used by a rate in
    // the same save, and a removed one can't leave a rate dangling.
    std::vector<SpeedProfile> speeds;
    if (in.containsKey("speedProfiles")) {
      JsonArray arr = in["speedProfiles"].as<JsonArray>();
      if (arr.size() == 0) { sendError(400, "need_one_speed_profile"); return; }
      if (arr.size() > MAX_SPEED_PROFILES) { sendError(400, "too_many_speed_profiles"); return; }
      for (JsonObject o : arr) {
        SpeedProfile sp{o["id"] | String(""), o["name"] | String(""), o["downMbps"] | 0.0f, o["upMbps"] | 0.0f};
        sp.name.trim();
        if (!sp.id.length() || sp.id.length() > 4 || !sp.name.length() || sp.name.length() > 24 ||
            sp.downMbps <= 0 || sp.upMbps <= 0) { sendError(400, "invalid_speed_profile"); return; }
        for (auto& other : speeds) {
          if (other.id == sp.id) { sendError(400, "duplicate_speed_profile"); return; }
        }
        speeds.push_back(sp);
      }
      // A subscriber still pointing at a removed speed (rates are checked below).
      for (uint32_t i = 0; i < _admin->subscriberCount(); i++) {
        bool found = false;
        for (auto& sp : speeds) if (sp.id == _admin->subscriberAt(i).tier) found = true;
        if (!found) { sendError(400, "speed_profile_in_use"); return; }
      }
    }
    const std::vector<SpeedProfile>& finalSpeeds = speeds.empty() ? _admin->speedProfiles : speeds;
    std::vector<RateProfile> rates;
    bool ratesGiven = in.containsKey("rateProfiles");
    if (ratesGiven) {
      String err = parseRateProfiles(in["rateProfiles"].as<JsonArray>(), finalSpeeds, rates);
      if (err.length()) { sendError(400, err); return; }
    } else {
      for (auto& r : _admin->rateProfiles) {
        bool found = false;
        for (auto& sp : finalSpeeds) if (sp.id == r.speedProfile) found = true;
        if (!found) { sendError(400, "speed_profile_in_use"); return; }
      }
    }
    String coinPin = in["coinPin"] | _admin->coinPin;
    String relayPin = in["relayPin"] | _admin->relayPin;
    if (coinPinGpio(coinPin) < 0 || (relayPin != "none" && coinPinGpio(relayPin) < 0)) {
      sendError(400, "invalid_pin"); return;
    }
    if (coinPin == relayPin) { sendError(400, "coin_and_relay_same_pin"); return; }
    uint16_t pulseValue = in["coinPulseValue"] | _admin->coinPulseValue;
    if (pulseValue < 1 || pulseValue > 100) { sendError(400, "invalid_pulse_value"); return; }
    String announcement = in["announcement"] | _admin->announcement;
    if (announcement.length() > MAX_ANNOUNCEMENT_LEN) announcement = announcement.substring(0, MAX_ANNOUNCEMENT_LEN);

    bool pinsChanged = coinPin != _admin->coinPin || relayPin != _admin->relayPin ||
                       (bool)(in["relayActiveHigh"] | _admin->relayActiveHigh) != _admin->relayActiveHigh;
    if (!speeds.empty()) _admin->speedProfiles = speeds;
    if (ratesGiven) _admin->rateProfiles = rates;
    _admin->announcement = announcement;
    _admin->coinPin = coinPin;
    _admin->relayPin = relayPin;
    _admin->relayActiveHigh = in["relayActiveHigh"] | _admin->relayActiveHigh;
    _admin->coinPulseValue = pulseValue;
    _admin->settingNightPromoEnabled = in["nightPromoEnabled"] | _admin->settingNightPromoEnabled;
    _admin->settingIdleTimeoutMin = in["idleTimeoutMin"] | _admin->settingIdleTimeoutMin;
    _admin->settingAutoRebootTime = in["autoRebootTime"] | _admin->settingAutoRebootTime;
    _admin->brandName = in["brandName"] | _admin->brandName;
    _admin->brandColor = in["brandColor"] | _admin->brandColor;
    _admin->soundEnabled = in["soundEnabled"] | _admin->soundEnabled;
    _admin->telegramEnabled = in["telegramEnabled"] | _admin->telegramEnabled;
    _admin->telegramBotToken = in["telegramBotToken"] | _admin->telegramBotToken;
    _admin->telegramChatId = in["telegramChatId"] | _admin->telegramChatId;
    // The request document (up to 6 KB) is no longer needed - free it
    // before saveSettings() builds its own, so the two don't have to fit
    // in the ESP8266's heap at the same time.
    in.clear();
    in.shrinkToFit();
    if (!_admin->saveSettings()) { sendError(507, "not_saved_low_memory"); return; }
    _admin->logEvent("settings_changed", "by " + admin->username);
    if (pinsChanged && onCoinPinsChanged) onCoinPinsChanged();

    // Keep the notifier's copy in sync immediately - it doesn't read
    // AdminAPI directly (kept decoupled, same pattern as MikrotikAPI's
    // host/credentials being copied in rather than read live), so a
    // Settings change needs to be pushed to it explicitly.
    _telegram->enabled = _admin->telegramEnabled;
    _telegram->botToken = _admin->telegramBotToken;
    _telegram->chatId = _admin->telegramChatId;

    // Push the new Mbps caps live to the MikroTik profiles that
    // actually enforce them - tier1/2/3 map to *-default/*-gaming/
    // *-high on both the hotspot (coin+voucher) and pppoe sides, so
    // one Settings save updates bandwidth for every access method at
    // once, including already-active sessions on that profile.
    // Attempted unconditionally, same as every other MikroTik-touching
    // handler in this file (handleLogin, handlePause, etc.) - gating on
    // the cached _mikrotikReachable flag first would skip a push that
    // could actually succeed, since that flag is only refreshed every
    // HOTSPOT_CHECK_INTERVAL and can be stale by up to that long.
    bool pushed = pushSpeedProfiles();

    DynamicJsonDocument out(128);
    out["ok"] = true;
    out["mikrotikPushed"] = pushed;
    sendJson(200, out);
  }

  // Every speed profile onto MikroTik: rate-limit updated live on
  // existing profiles (active users included), admin-added ones created.
  // PPPoE twins are best-effort - a router without the PPPoE feature has
  // no pool for them - so only the hotspot side decides "pushed".
  bool pushSpeedProfiles() {
    bool pushed = true;
    for (auto& sp : _admin->speedProfiles) {
      String rateLimit = _admin->rateLimitStringFor(sp.id);
      bool ok = _api->ensureHotspotUserProfile(QoSManager::hotspotProfile(sp.id), rateLimit);
      pushed &= ok;
      // Router unreachable: stop instead of waiting out a timeout per
      // profile (the admin's Save would hang for many seconds).
      if (!ok && !_api->isConnected()) return false;
      _api->ensurePppProfile(QoSManager::pppoeProfile(sp.id), rateLimit);
    }
    return pushed;
  }

  void handleAdminImportVouchers() {
    if (!requireAdmin()) return;
    uint16_t imported = _admin->importCsv(_server->arg("plain"));
    DynamicJsonDocument out(64);
    out["imported"] = imported;
    sendJson(200, out);
  }

  void handleAdminGenerateVouchers() {
    if (!requireAdmin()) return;
    DynamicJsonDocument in(128);
    if (deserializeJson(in, _server->arg("plain"))) { sendError(400, "bad_json"); return; }
    uint32_t pesoAmount = in["pesoAmount"] | 0;
    uint8_t count = (uint8_t)constrain((int)(in["count"] | 1), 1, 20);

    std::vector<String> codes = _admin->generateVouchers(pesoAmount, count);
    if (codes.empty()) { sendError(400, "no_such_rate_profile"); return; }
    DynamicJsonDocument out(512);
    JsonArray arr = out.to<JsonArray>();
    for (auto& c : codes) arr.add(c);
    sendJson(200, out);
  }

  void handleAdminSubscribersList() {
    AdminAccount* admin = requireAdmin(/*requireSuper=*/true);
    if (!admin) return;
    beginStream();
    _server->sendContent("[");
    bool first = true;
    for (uint32_t i = 0; i < _admin->subscriberCount(); i++) {
      Subscriber& s = _admin->subscriberAt(i);
      StaticJsonDocument<256> o;
      o["username"] = s.username;
      o["tier"] = s.tier;
      o["expiryEpoch"] = s.expiryEpoch;
      o["active"] = s.active;
      o["expired"] = _admin->isSubscriberExpired(s);
      streamItem(o, first);
    }
    _server->sendContent("]");
    endStream();
  }

  void handleAdminAddSubscriber() {
    AdminAccount* admin = requireAdmin(/*requireSuper=*/true);
    if (!admin) return;
    DynamicJsonDocument in(256);
    if (deserializeJson(in, _server->arg("plain"))) { sendError(400, "bad_json"); return; }
    String username = in["username"] | "";
    String password = in["password"] | "";
    String tier = in["tier"] | "1";
    uint16_t days = (uint16_t)constrain((int)(in["days"] | 30), 1, 3650);
    float price = in["price"] | 0.0f;
    username.trim();
    if (!_admin->speedById(tier)) { sendError(400, "unknown_speed_profile"); return; }
    if (!username.length() || !password.length()) {
      sendError(400, "missing_username_or_password");
      return;
    }
    if (!AdminAPI::clockSynced()) { sendError(503, "clock_not_synced_try_again"); return; }
    if (!_admin->addSubscriber(username, password, tier, days, price)) {
      sendError(400, "username_taken_or_table_full");
      return;
    }
    _admin->logEvent("subscriber_added", username + " by " + admin->username);
    sendOk();
  }

  void handleAdminRenewSubscriber() {
    AdminAccount* admin = requireAdmin(/*requireSuper=*/true);
    if (!admin) return;
    DynamicJsonDocument in(128);
    if (deserializeJson(in, _server->arg("plain"))) { sendError(400, "bad_json"); return; }
    String username = in["username"] | "";
    uint16_t days = (uint16_t)constrain((int)(in["days"] | 30), 1, 3650);
    float price = in["price"] | 0.0f;
    if (!AdminAPI::clockSynced()) { sendError(503, "clock_not_synced_try_again"); return; }
    if (!_admin->renewSubscriber(username, days, price)) { sendError(404, "subscriber_not_found"); return; }
    _admin->logEvent("subscriber_renewed", username + " +" + String(days) + "d by " + admin->username);
    sendOk();
  }

  void handleAdminToggleSubscriber() {
    AdminAccount* admin = requireAdmin(/*requireSuper=*/true);
    if (!admin) return;
    DynamicJsonDocument in(128);
    if (deserializeJson(in, _server->arg("plain"))) { sendError(400, "bad_json"); return; }
    String username = in["username"] | "";
    bool active = in["active"] | true;
    if (!_admin->setSubscriberActive(username, active)) { sendError(404, "subscriber_not_found"); return; }
    _admin->logEvent("subscriber_toggled", username + " -> " + (active ? "active" : "disabled") + " by " + admin->username);
    sendOk();
  }

  // ---- Admin accounts (super only) ---------------------------------------

  void handleAdminAccountsList() {
    AdminAccount* admin = requireAdmin(/*requireSuper=*/true);
    if (!admin) return;
    DynamicJsonDocument out(1024);
    JsonArray arr = out.to<JsonArray>();
    for (uint32_t i = 0; i < _admin->adminAccountCount(); i++) {
      AdminAccount& a = _admin->adminAccountAt(i);
      JsonObject o = arr.createNestedObject();
      o["username"] = a.username;
      o["role"] = a.role;
      o["active"] = a.active;
    }
    sendJson(200, out);
  }

  void handleAdminAddAccount() {
    AdminAccount* admin = requireAdmin(/*requireSuper=*/true);
    if (!admin) return;
    DynamicJsonDocument in(256);
    if (deserializeJson(in, _server->arg("plain"))) { sendError(400, "bad_json"); return; }
    String username = in["username"] | "";
    String password = in["password"] | "";
    String role = in["role"] | "staff";
    username.trim();
    if (!username.length() || !password.length()) {
      sendError(400, "missing_username_or_password");
      return;
    }
    if (!_admin->addAdminAccount(username, password, role)) {
      sendError(400, "username_taken_invalid_role_or_table_full");
      return;
    }
    _admin->logEvent("admin_account_added", username + " (" + role + ") by " + admin->username);
    sendOk();
  }

  void handleAdminToggleAccount() {
    AdminAccount* admin = requireAdmin(/*requireSuper=*/true);
    if (!admin) return;
    DynamicJsonDocument in(128);
    if (deserializeJson(in, _server->arg("plain"))) { sendError(400, "bad_json"); return; }
    String username = in["username"] | "";
    bool active = in["active"] | true;
    if (!_admin->setAdminAccountActive(username, active)) {
      sendError(400, "not_found_or_last_super_admin");
      return;
    }
    _admin->logEvent("admin_account_toggled", username + " -> " + (active ? "active" : "disabled") + " by " + admin->username);
    sendOk();
  }

  // ---- Activity log + Sales (either role can view) -----------------------

  void handleAdminLogs() {
    if (!requireAdmin()) return;
    beginStream();
    _server->sendContent("[");
    bool first = true;
    uint32_t n = _admin->activityLogCount();
    for (uint32_t i = 0; i < n; i++) {
      ActivityLogEntry& e = _admin->activityLogAt(i);
      StaticJsonDocument<256> o;
      o["epoch"] = e.epoch;
      o["type"] = e.type;
      o["detail"] = e.detail;
      streamItem(o, first);
    }
    _server->sendContent("]");
    endStream();
  }

  void handleAdminSales() {
    if (!requireAdmin()) return;
    beginStream();
    _server->sendContent("{\"history\":[");
    bool first = true;
    for (uint32_t i = 0; i < _admin->salesHistoryCount(); i++) {
      DailySalesEntry& e = _admin->salesHistoryAt(i);
      DynamicJsonDocument o(768);
      o["dateStamp"] = e.dateStamp;
      o["coinRevenue"] = e.coinRevenue;
      AdminAPI::writeByVendo(o.as<JsonObject>(), e.byVendo);
      o["voucherRevenue"] = e.voucherRevenue;
      o["subscriptionRevenue"] = e.subscriptionRevenue;
      o["users"] = e.users;
      o["dataUsedBytes"] = (double)e.dataUsedBytes;
      streamItem(o, first);
    }
    // Today's still-live totals, so the Sales tab can show it alongside history.
    DynamicJsonDocument today(768);
    today["dateStamp"] = _admin->todayDateStamp;
    today["coinRevenue"] = _admin->coinRevenueToday;
    AdminAPI::writeByVendo(today.as<JsonObject>(), _admin->coinByVendoToday);
    today["voucherRevenue"] = _admin->voucherRevenueToday;
    today["subscriptionRevenue"] = _admin->subscriptionRevenueToday;
    today["users"] = _admin->usersToday;
    today["dataUsedBytes"] = (double)_admin->dataUsedTodayBytes;
    String t;
    serializeJson(today, t);
    _server->sendContent("],\"today\":" + t + "}");
    endStream();
  }
};

#endif // GUI_HANDLER_H
