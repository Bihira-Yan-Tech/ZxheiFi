/*
 * session.h - Active session tracking
 * ZXHEIFI - NodeMCU ESP8266 Firmware
 *
 * Tracks concurrently-active hotspot/PPPoE sessions in RAM, deducts
 * time+data together, applies the idle auto-pause rule, and persists
 * the table to SPIFFS every SESSION_SAVE_INTERVAL so sessions survive a
 * power cycle (crash/brownout recovery).
 */
#ifndef SESSION_H
#define SESSION_H

#include <Arduino.h>
#include <FS.h>
#include <ArduinoJson.h>
#include "config.h"

struct SessionEntry {
  String sessionId;          // = voucher code, also used as the MikroTik username
  String mode;                // "hotspot" or "pppoe"
  String tier;                 // "1", "2", "3", "night", or "subscription"
  uint32_t remainingSeconds;
  uint64_t remainingBytes;
  uint64_t lastBytesSeen;      // last bytes-in+out reading from MikroTik, for delta calc
  uint32_t lastActivityMs;     // millis() of last non-idle traffic sample
  bool active;
  bool isSubscriber;           // true = unlimited subscription, not a consumable voucher
  bool paused;                 // true = manually paused, not provisioned on MikroTik right now
  uint32_t pausedAtMs;          // millis() when Pause was pressed, for pauseWindowMinutes enforcement
  uint32_t pauseWindowMinutes;  // how long this session may sit paused before forfeiture - from
                                  // the matching RateProfile::validityMinutes (falls back to
                                  // MAX_PAUSE_MINUTES for subscriptions/night-promo, which have none)
};

class SessionManager {
public:
  void begin() {
    _count = 0;
    if (!SPIFFS.exists(SESSIONS_FILE)) return;

    File f = SPIFFS.open(SESSIONS_FILE, "r");
    if (!f) return;

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
      Serial.printf("SessionManager: sessions.json parse failed (%s), starting empty\n", err.c_str());
      return;
    }

    for (JsonObject o : doc.as<JsonArray>()) {
      if (_count >= MAX_SESSIONS) break;
      SessionEntry& e = _entries[_count++];
      e.sessionId = o["sessionId"].as<String>();
      e.mode = o["mode"].as<String>();
      e.tier = o["tier"].as<String>();
      e.remainingSeconds = o["remainingSeconds"] | 0;
      e.remainingBytes = o["remainingBytes"].as<uint64_t>();
      e.lastBytesSeen = 0;
      e.lastActivityMs = millis();
      e.active = o["active"] | false;
      e.isSubscriber = o["isSubscriber"] | false;
      e.paused = o["paused"] | false;
      e.pausedAtMs = e.paused ? millis() : 0; // pause clock restarts across a reboot
      e.pauseWindowMinutes = o["pauseWindowMinutes"] | (uint32_t)MAX_PAUSE_MINUTES;
    }
    Serial.printf("SessionManager: recovered %u session(s) from SPIFFS\n", _count);
  }

  void saveToSPIFFS() {
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < _count; i++) {
      if (!_entries[i].active) continue;
      JsonObject o = arr.createNestedObject();
      o["sessionId"] = _entries[i].sessionId;
      o["mode"] = _entries[i].mode;
      o["tier"] = _entries[i].tier;
      o["remainingSeconds"] = _entries[i].remainingSeconds;
      o["remainingBytes"] = (double)_entries[i].remainingBytes; // ArduinoJson lacks native uint64_t I/O
      o["active"] = _entries[i].active;
      o["isSubscriber"] = _entries[i].isSubscriber;
      o["paused"] = _entries[i].paused;
      o["pauseWindowMinutes"] = _entries[i].pauseWindowMinutes;
    }
    File f = SPIFFS.open(SESSIONS_FILE, "w");
    if (!f) return;
    serializeJson(doc, f);
    f.close();
  }

  // Creates (or replaces) the in-RAM entry for a freshly redeemed voucher
  // or a logged-in subscriber.
  SessionEntry* addSession(const String& sessionId, const String& mode, const String& tier,
                            uint32_t seconds, uint64_t bytes, bool isSubscriber = false,
                            uint32_t pauseWindowMinutes = MAX_PAUSE_MINUTES) {
    SessionEntry* e = findSession(sessionId);
    if (!e) {
      // Reuse a slot freed by a disconnected/expired session. Slots used
      // to be append-only, so after MAX_SESSIONS logins since boot every
      // new customer silently went untracked.
      for (uint8_t i = 0; i < _count && !e; i++) {
        if (!_entries[i].active) e = &_entries[i];
      }
      if (!e) {
        if (_count >= MAX_SESSIONS) return nullptr;
        e = &_entries[_count++];
      }
    }
    e->sessionId = sessionId;
    e->mode = mode;
    e->tier = tier;
    e->remainingSeconds = seconds;
    e->remainingBytes = bytes;
    e->lastBytesSeen = 0;
    e->lastActivityMs = millis();
    e->active = true;
    e->isSubscriber = isSubscriber;
    e->paused = false;
    e->pausedAtMs = 0;
    e->pauseWindowMinutes = pauseWindowMinutes;
    return e;
  }

  // Marks a session paused - caller is responsible for actually
  // deprovisioning it on MikroTik first (see PPPoEManager::deprovision).
  void pauseSession(const String& sessionId) {
    SessionEntry* e = findSession(sessionId);
    if (!e || e->paused) return;
    e->paused = true;
    e->pausedAtMs = millis();
  }

  // Un-pauses so tickSeconds()/applyTrafficSample() resume counting -
  // caller is responsible for re-provisioning on MikroTik first.
  void resumeSession(const String& sessionId) {
    SessionEntry* e = findSession(sessionId);
    if (!e) return;
    e->paused = false;
    e->lastActivityMs = millis(); // don't let paused time count as idle either
  }

  bool isPaused(const String& sessionId) {
    SessionEntry* e = findSession(sessionId);
    return e && e->paused;
  }

  // True once a paused session has sat idle past MAX_PAUSE_MINUTES -
  // the caller should fully remove it rather than let it be resumed.
  bool isPausedTooLong(const String& sessionId) {
    SessionEntry* e = findSession(sessionId);
    if (!e || !e->paused) return false;
    return (millis() - e->pausedAtMs) > e->pauseWindowMinutes * 60000UL;
  }

  SessionEntry* findSession(const String& sessionId) {
    for (uint8_t i = 0; i < _count; i++) {
      if (_entries[i].active && _entries[i].sessionId == sessionId) return &_entries[i];
    }
    return nullptr;
  }

  void removeSession(const String& sessionId) {
    SessionEntry* e = findSession(sessionId);
    if (e) e->active = false;
  }

  void extend(const String& sessionId, uint32_t addSeconds, uint64_t addBytes) {
    SessionEntry* e = findSession(sessionId);
    if (!e) return;
    e->remainingSeconds += addSeconds;
    e->remainingBytes += addBytes;
  }

  // Called from the main loop with the whole seconds elapsed since the
  // last call. Only zeroes the budget - the caller sees isOver() and
  // revokes the account on MikroTik before removing the entry. (Marking
  // the entry inactive here used to hide it from that loop.)
  void tickSeconds(const String& sessionId, uint32_t elapsedSeconds) {
    SessionEntry* e = findSession(sessionId);
    if (!e || e->paused || isIdle(sessionId)) return; // paused/idle sessions are not charged
    e->remainingSeconds = elapsedSeconds >= e->remainingSeconds ? 0 : e->remainingSeconds - elapsedSeconds;
  }

  // Feeds a fresh bytes-in+out reading from MikroTik for a session.
  // Deducts the delta from the data budget and refreshes the idle clock
  // when meaningful traffic was seen.
  // Returns the bytes used since the previous sample (for the day's
  // "Data Used" total).
  uint64_t applyTrafficSample(const String& sessionId, uint64_t totalBytesNow) {
    SessionEntry* e = findSession(sessionId);
    if (!e || e->paused) return 0;

    uint64_t delta = (totalBytesNow >= e->lastBytesSeen) ? (totalBytesNow - e->lastBytesSeen) : 0;
    e->lastBytesSeen = totalBytesNow;

    if (delta >= IDLE_ACTIVITY_BYTES) {
      e->lastActivityMs = millis();
    }

    // Same as tickSeconds(): zero the budget and let the main loop revoke
    // it. Deactivating here meant a data-exhausted PPPoE secret (which has
    // no byte cap of its own on MikroTik) was never removed.
    e->remainingBytes = delta >= e->remainingBytes ? 0 : e->remainingBytes - delta;
    return delta;
  }

  bool isIdle(const String& sessionId) {
    SessionEntry* e = findSession(sessionId);
    if (!e) return false;
    return (millis() - e->lastActivityMs) > IDLE_TIMEOUT_MS;
  }

  bool isOver(const String& sessionId) {
    SessionEntry* e = findSession(sessionId);
    return !e || !e->active || e->remainingSeconds == 0 || e->remainingBytes == 0;
  }

  uint8_t count() const { return _count; }
  SessionEntry& entryAt(uint8_t i) { return _entries[i]; }

private:
  SessionEntry _entries[MAX_SESSIONS];
  uint8_t _count = 0;
};

#endif // SESSION_H
