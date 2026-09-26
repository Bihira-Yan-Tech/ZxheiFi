/*
 * mikrotik_api.h - Minimal RouterOS API client
 * ZXHEIFI - NodeMCU ESP8266 Firmware
 *
 * Implements the RouterOS binary API protocol (plaintext login, RouterOS
 * >= 6.43) over a plain TCP socket to MIKROTIK_API_PORT. Reference:
 * https://help.mikrotik.com/docs/display/ROS/API
 *
 * Only the small subset needed by ZxheiFi is implemented: login,
 * sentence send/receive, and helpers for hotspot users / PPP secrets.
 */
#ifndef MIKROTIK_API_H
#define MIKROTIK_API_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <vector>
#include "config.h"

struct ApiAttr {
  String key;
  String value;
};

class MikrotikAPI {
public:
  bool loggedIn = false;

  // Runtime-configurable so the Setup Wizard's saved network.json can
  // override these without a recompile - default to the config.h
  // constants so nothing changes for a device that never ran the
  // wizard (dev/testing, or a pre-wizard build).
  String host = NODEMCU_GATEWAY;
  String apiUser = MIKROTIK_API_USER;
  String apiPass = MIKROTIK_API_PASS;

  // Establishes the TCP connection + API login. Safe to call repeatedly;
  // no-ops if already connected and logged in.
  bool begin() {
    if (loggedIn && _client.connected()) return true;
    _client.stop();
    loggedIn = false;

    if (!_client.connect(host.c_str(), MIKROTIK_API_PORT)) {
      return false;
    }
    _client.setTimeout(3000);

    std::vector<String> words;
    words.push_back("/login");
    words.push_back(String("=name=") + apiUser);
    words.push_back(String("=password=") + apiPass);
    if (!sendSentence(words)) return false;

    std::vector<ApiAttr> attrs;
    String tag;
    bool ok = readReply(attrs, tag) == ReplyDone;
    loggedIn = ok;
    if (!ok) _client.stop();
    return ok;
  }

  bool isConnected() { return loggedIn && _client.connected(); }

  void end() {
    _client.stop();
    loggedIn = false;
  }

  // Runs a command sentence, collects attribute words from every !re
  // reply into `results` (one vector<ApiAttr> per row), until !done/!trap.
  // Returns false on !trap or connection failure.
  bool run(const std::vector<String>& words, std::vector<std::vector<ApiAttr>>* results = nullptr) {
    if (!begin()) return false;
    if (!sendSentence(words)) { loggedIn = false; return false; }

    // RouterOS always sends a trailing !done after a !trap (confirmed
    // against real RouterOS 7.16.2, not just the API docs) - a command's
    // reply isn't fully consumed until that !done arrives. Returning
    // early on !trap leaves it unread in the socket, so the NEXT run()
    // call's first readReply() picks up that stale !done instead of its
    // own command's real reply - silently desyncing every reply after
    // the first router-side rejection for the rest of the connection's
    // life. Keep looping through the trap and only return once !done
    // is actually seen.
    bool trapped = false;
    while (true) {
      std::vector<ApiAttr> attrs;
      String tag;
      ReplyType type = readReply(attrs, tag);
      if (type == ReplyRow) {
        if (results) results->push_back(attrs);
      } else if (type == ReplyEmpty) {
        // RouterOS 7.18+ answers a print with no matches with !empty
        // (then !done). Treating it as an unknown reply used to drop the
        // connection on every "does this user exist?" lookup.
      } else if (type == ReplyTrap) {
        trapped = true;
      } else if (type == ReplyDone) {
        return !trapped;
      } else { // ReplyError: connection/parse failure, nothing left to drain
        loggedIn = false;
        return false;
      }
    }
  }

  // ---- High level helpers -------------------------------------------------

  // limitUptimeSec/limitBytesTotal of UNLIMITED_SECONDS/UNLIMITED_BYTES
  // (see config.h) omit that RouterOS parameter entirely rather than
  // sending a huge number - that's what actually means "no limit" to
  // RouterOS, and avoids relying on a giant duration/byte string
  // parsing the same way a normal one would.
  bool addHotspotUser(const String& name, const String& password, const String& profile,
                       uint32_t limitUptimeSec, uint64_t limitBytesTotal) {
    removeByName("/ip/hotspot/user", name); // avoid duplicate-name errors on re-login
    std::vector<String> w;
    w.push_back("/ip/hotspot/user/add");
    w.push_back("=name=" + name);
    w.push_back("=password=" + password);
    w.push_back("=profile=" + profile);
    if (limitUptimeSec != UNLIMITED_SECONDS) {
      w.push_back("=limit-uptime=" + secondsToHms(limitUptimeSec));
    }
    if (limitBytesTotal != UNLIMITED_BYTES) {
      w.push_back("=limit-bytes-total=" + String(limitBytesTotal));
    }
    return run(w);
  }

  // Removes ONLY the active session (if any), leaving the /ip/hotspot/user
  // record itself untouched. Deliberately separate from addHotspotUser()
  // - that path is also used by Extend, where kicking the still-connected
  // client mid-extend would be a regression (the whole point of Extend is
  // a seamless top-up). This exists for the opposite case: a customer
  // reconnecting under a NEW MAC (phones that randomize MAC per
  // reconnect - see gui_handler.h's handleLogin() reconnect branch),
  // where the OLD MAC's active entry is genuinely stale and would
  // otherwise block the new MAC's login under shared-users=1.
  bool kickActiveHotspotUser(const String& name) {
    String activeId = findIdByQuery("/ip/hotspot/active", "user", name);
    if (!activeId.length()) return true; // nothing stale to kick
    std::vector<String> w;
    w.push_back("/ip/hotspot/active/remove");
    w.push_back("=.id=" + activeId);
    return run(w);
  }

  bool removeHotspotUser(const String& name) {
    // Also kick any active session so the change takes effect immediately.
    kickActiveHotspotUser(name);
    return removeByName("/ip/hotspot/user", name);
  }

  // Looks up an active hotspot session's MAC by username - used by
  // GUIHandler::handleAdminBlock() to know WHICH device to actually
  // block (blocking a sessionId/voucher code alone wouldn't stop the
  // same phone from just logging in again with a different code).
  String activeSessionMac(const String& name) {
    std::vector<String> w;
    w.push_back("/ip/hotspot/active/print");
    w.push_back("?user=" + name);
    std::vector<std::vector<ApiAttr>> rows;
    if (!run(w, &rows) || rows.empty()) return "";
    for (auto& a : rows[0]) {
      if (a.key == "mac-address") return a.value;
    }
    return "";
  }

  // Admin-initiated block: a persistent ip-binding, enforced by
  // RouterOS itself at the walled-garden layer - holds even across a
  // NodeMCU reboot, unlike anything tracked only in AdminAPI.
  bool blockMac(const String& mac) {
    // Re-blocking (or a MAC that already has a binding, e.g. an old
    // bypass) must not stack a second entry - RouterOS rejects a
    // duplicate MAC binding, which made a repeat Block fail.
    String id = findIdByQuery("/ip/hotspot/ip-binding", "mac-address", mac);
    if (id.length()) {
      std::vector<String> s;
      s.push_back("/ip/hotspot/ip-binding/set");
      s.push_back("=.id=" + id);
      s.push_back("=type=blocked");
      s.push_back("=comment=ZxheiFi admin block");
      return run(s);
    }
    std::vector<String> w;
    w.push_back("/ip/hotspot/ip-binding/add");
    w.push_back("=type=blocked");
    w.push_back("=mac-address=" + mac);
    w.push_back("=comment=ZxheiFi admin block");
    return run(w);
  }

  bool unblockMac(const String& mac) {
    String id = findIdByQuery("/ip/hotspot/ip-binding", "mac-address", mac);
    if (!id.length()) return true; // nothing to remove is not an error
    std::vector<String> w;
    w.push_back("/ip/hotspot/ip-binding/remove");
    w.push_back("=.id=" + id);
    return run(w);
  }

  bool addPppSecret(const String& name, const String& password, const String& profile) {
    removeByName("/ppp/secret", name);
    std::vector<String> w;
    w.push_back("/ppp/secret/add");
    w.push_back("=name=" + name);
    w.push_back("=password=" + password);
    w.push_back("=service=pppoe");
    w.push_back("=profile=" + profile);
    return run(w);
  }

  bool removePppSecret(const String& name) {
    String activeId = findIdByQuery("/ppp/active", "name", name);
    if (activeId.length()) {
      std::vector<String> w;
      w.push_back("/ppp/active/remove");
      w.push_back("=.id=" + activeId);
      run(w);
    }
    return removeByName("/ppp/secret", name);
  }

  // Returns true and fills bytesIn/bytesOut if the given hotspot/PPPoE
  // user currently has an active session.
  bool getActiveTraffic(const String& name, bool pppoeMode, uint64_t& bytesIn, uint64_t& bytesOut) {
    std::vector<String> w;
    w.push_back(pppoeMode ? "/ppp/active/print" : "/ip/hotspot/active/print");
    w.push_back(String("?") + (pppoeMode ? "name=" : "user=") + name);
    std::vector<std::vector<ApiAttr>> rows;
    if (!run(w, &rows) || rows.empty()) return false;
    bytesIn = 0;
    bytesOut = 0;
    for (auto& a : rows[0]) {
      if (a.key == "bytes-in") bytesIn = strtoull(a.value.c_str(), nullptr, 10);
      if (a.key == "bytes-out") bytesOut = strtoull(a.value.c_str(), nullptr, 10);
    }
    return true;
  }

  bool ping() {
    std::vector<String> w;
    w.push_back("/system/identity/print");
    return run(w);
  }

  // Pushes a new "rx/tx" rate-limit (e.g. "5M/10M") live to an existing
  // hotspot user profile or ppp profile by name - used by
  // GUIHandler::handleAdminSaveSettings() so an admin's Mbps change in
  // Settings takes effect immediately for every session on that
  // profile, not just future ones. Returns false if the profile name
  // isn't found or the router is unreachable.
  bool setHotspotProfileRateLimit(const String& profileName, const String& rateLimit) {
    return setRateLimitByName("/ip/hotspot/user/profile", profileName, rateLimit);
  }

  bool setPppProfileRateLimit(const String& profileName, const String& rateLimit) {
    return setRateLimitByName("/ppp/profile", profileName, rateLimit);
  }

  // Settings > Speed Profiles: set the rate-limit on an existing profile,
  // or create it (admin-added speeds don't exist on the router yet).
  bool ensureHotspotUserProfile(const String& name, const String& rateLimit) {
    if (findIdByQuery("/ip/hotspot/user/profile", "name", name).length()) {
      return setRateLimitByName("/ip/hotspot/user/profile", name, rateLimit);
    }
    std::vector<String> w;
    w.push_back("/ip/hotspot/user/profile/add");
    w.push_back("=name=" + name);
    w.push_back("=rate-limit=" + rateLimit);
    w.push_back("=shared-users=1");
    w.push_back("=transparent-proxy=no");
    return run(w);
  }

  // PPPoE twin - same addressing as the pppoe-* profiles Config creates.
  // Fails harmlessly on a router without the PPPoE feature (no pool).
  bool ensurePppProfile(const String& name, const String& rateLimit) {
    if (findIdByQuery("/ppp/profile", "name", name).length()) {
      return setRateLimitByName("/ppp/profile", name, rateLimit);
    }
    std::vector<String> w;
    w.push_back("/ppp/profile/add");
    w.push_back("=name=" + name);
    w.push_back("=rate-limit=" + rateLimit);
    w.push_back("=local-address=10.0.10.1");
    w.push_back("=remote-address=zxheifi-pppoe-pool");
    w.push_back("=dns-server=10.0.10.1");
    w.push_back("=only-one=yes");
    w.push_back("=comment=zxheifi");
    return run(w);
  }

  static String secondsToHms(uint32_t totalSeconds) {
    uint32_t h = totalSeconds / 3600;
    uint32_t m = (totalSeconds % 3600) / 60;
    uint32_t s = totalSeconds % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u", h, m, s);
    return String(buf);
  }

private:
  WiFiClient _client;

  enum ReplyType { ReplyRow, ReplyDone, ReplyTrap, ReplyEmpty, ReplyError };

  String findIdByQuery(const String& path, const String& field, const String& value) {
    std::vector<String> w;
    w.push_back(path + "/print");
    w.push_back("?" + field + "=" + value);
    std::vector<std::vector<ApiAttr>> rows;
    if (!run(w, &rows) || rows.empty()) return "";
    for (auto& a : rows[0]) {
      if (a.key == ".id") return a.value;
    }
    return "";
  }

  bool removeByName(const String& path, const String& name) {
    String id = findIdByQuery(path, "name", name);
    if (!id.length()) return true; // nothing to remove is not an error
    std::vector<String> w;
    w.push_back(path + "/remove");
    w.push_back("=.id=" + id);
    return run(w);
  }

  bool setRateLimitByName(const String& path, const String& name, const String& rateLimit) {
    String id = findIdByQuery(path, "name", name);
    if (!id.length()) return false; // profile must already exist (created by the .rsc scripts)
    std::vector<String> w;
    w.push_back(path + "/set");
    w.push_back("=.id=" + id);
    w.push_back("=rate-limit=" + rateLimit);
    return run(w);
  }

  void writeLen(uint32_t len) {
    if (len < 0x80) {
      _client.write((uint8_t)len);
    } else if (len < 0x4000) {
      len |= 0x8000;
      _client.write((uint8_t)(len >> 8));
      _client.write((uint8_t)(len & 0xFF));
    } else if (len < 0x200000) {
      len |= 0xC00000;
      _client.write((uint8_t)(len >> 16));
      _client.write((uint8_t)(len >> 8));
      _client.write((uint8_t)(len & 0xFF));
    } else {
      len |= 0xE0000000;
      _client.write((uint8_t)(len >> 24));
      _client.write((uint8_t)(len >> 16));
      _client.write((uint8_t)(len >> 8));
      _client.write((uint8_t)(len & 0xFF));
    }
  }

  // Blocking single-byte read with the client's configured timeout.
  // Returns -1 on timeout/disconnect.
  int readByte() {
    uint32_t start = millis();
    while (!_client.available()) {
      if (!_client.connected() || millis() - start > 3000) return -1;
      delay(1);
    }
    return _client.read();
  }

  int32_t readLen() {
    int c1 = readByte();
    if (c1 < 0) return -1;
    if ((c1 & 0x80) == 0x00) {
      return c1;
    } else if ((c1 & 0xC0) == 0x80) {
      int c2 = readByte(); if (c2 < 0) return -1;
      return ((c1 & 0x3F) << 8) | c2;
    } else if ((c1 & 0xE0) == 0xC0) {
      int c2 = readByte(); int c3 = readByte();
      if (c2 < 0 || c3 < 0) return -1;
      return ((c1 & 0x1F) << 16) | (c2 << 8) | c3;
    } else if ((c1 & 0xF0) == 0xE0) {
      int c2 = readByte(); int c3 = readByte(); int c4 = readByte();
      if (c2 < 0 || c3 < 0 || c4 < 0) return -1;
      return ((c1 & 0x0F) << 24) | (c2 << 16) | (c3 << 8) | c4;
    }
    return -1; // 5-byte length words are not needed for our short commands
  }

  bool sendWord(const String& word) {
    writeLen(word.length());
    if (word.length()) _client.print(word);
    return _client.connected();
  }

  bool sendSentence(const std::vector<String>& words) {
    for (auto& w : words) {
      if (!sendWord(w)) return false;
    }
    writeLen(0); // terminator
    return true;
  }

  bool readWord(String& out) {
    int32_t len = readLen();
    if (len < 0) return false;
    if (len == 0) { out = ""; return true; }
    out = "";
    out.reserve(len);
    for (int32_t i = 0; i < len; i++) {
      int b = readByte();
      if (b < 0) return false;
      out += (char)b;
    }
    return true;
  }

  // Reads exactly one sentence (!re / !done / !trap and its attribute
  // words) and classifies it.
  ReplyType readReply(std::vector<ApiAttr>& attrs, String& tag) {
    std::vector<String> words;
    while (true) {
      String w;
      if (!readWord(w)) return ReplyError;
      if (w.length() == 0) break; // end of sentence
      words.push_back(w);
    }
    if (words.empty()) return ReplyError;

    ReplyType type = ReplyRow;
    if (words[0] == "!done") type = ReplyDone;
    else if (words[0] == "!trap") type = ReplyTrap;
    else if (words[0] == "!re") type = ReplyRow;
    else if (words[0] == "!empty") type = ReplyEmpty;
    else return ReplyError;

    for (size_t i = 1; i < words.size(); i++) {
      const String& w = words[i];
      if (w.length() && w[0] == '=') {
        int eq = w.indexOf('=', 1);
        if (eq > 0) {
          ApiAttr a;
          a.key = w.substring(1, eq);
          a.value = w.substring(eq + 1);
          attrs.push_back(a);
        }
      } else if (w.startsWith(".tag=")) {
        tag = w.substring(5);
      }
    }
    // RouterOS sends !done right after the sentence that carries results
    // for single-object replies too; treat trailing !done after a !re the
    // same as ReplyDone by letting the caller loop until it sees it.
    return type;
  }
};

#endif // MIKROTIK_API_H
