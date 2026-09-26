/*
 * coin_slot.h - Coin sessions: who the coins belong to
 * ZXHEIFI - NodeMCU ESP8266 Firmware
 *
 * The coin acceptor only reports pulses - it can't know which phone is
 * paying. Coins used to become an anonymous "COIN-<millis>" hotspot user
 * that nobody could log in as, so a customer's money bought nothing.
 *
 * Flow now (same idea as the original JuanFi):
 *   1. login.html "Insert Coin" -> POST /api/coin/start. The slot is
 *      reserved for that browser (a random token) and the relay
 *      (Settings > Coin Slot, default D7) switches on so the acceptor
 *      takes coins; with it off, the acceptor rejects coins.
 *   2. Each coin looks up its own Rate Profile (exact peso match) and
 *      adds that profile's minutes/data to the reservation.
 *   3. "Done" -> POST /api/coin/done, or COIN_IDLE_TIMEOUT_MS without a
 *      new coin: the credit is provisioned on MikroTik under a fresh
 *      code (or added to the customer's running session when they came
 *      from status.html), and login.html logs them in with it.
 * Only one customer can hold the slot at a time; a second one gets
 * "coin_slot_busy" with the seconds left until it frees up.
 */
#ifndef COIN_SLOT_H
#define COIN_SLOT_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "session.h"
#include "admin_api.h"
#include "pppoe.h"
#include "qos.h"
#include "mikrotik_api.h"
#include "telegram.h"

class CoinSlot {
public:
  void begin(SessionManager& sessions, AdminAPI& admin, PPPoEManager& pppoe,
             QoSManager& qos, MikrotikAPI& api, TelegramNotifier& telegram) {
    _sessions = &sessions;
    _admin = &admin;
    _pppoe = &pppoe;
    _qos = &qos;
    _api = &api;
    _telegram = &telegram;
    applyRelayPin();
  }

  // Settings > Coin Slot: relay pin and whether it switches on HIGH or LOW
  // (relay modules come both ways). Safe to call again after a change.
  void applyRelayPin() {
    if (_relayGpio >= 0) pinMode(_relayGpio, INPUT);   // release the old pin
    _relayGpio = coinPinGpio(_admin->relayPin);
    if (_relayGpio >= 0) pinMode(_relayGpio, OUTPUT);
    setRelay(_reserved);
  }

  // ---- called from the HTTP handlers ---------------------------------

  // Returns "" on success (token filled in), or an error code.
  String start(const String& mac, const String& extendSession, String& token, uint32_t& waitSec) {
    if (_reserved && token.length() && token == _token) { // same customer tapping again
      _lastActivityMs = millis();
      return "";
    }
    if (_reserved) {
      uint32_t idle = millis() - _lastActivityMs;
      uint32_t limit = _seconds ? COIN_IDLE_TIMEOUT_MS : COIN_NO_COIN_TIMEOUT_MS;
      waitSec = idle < limit ? (limit - idle) / 1000 + 1 : 1;
      return "coin_slot_busy";
    }
    clearCredit();
    _reserved = true;
    _token = randomCode(12);
    token = _token;
    _mac = mac;
    _extendSession = extendSession;
    _lastActivityMs = millis();
    setRelay(true);

    // A coin dropped just before tapping Insert Coin was almost certainly
    // this customer's - credit it instead of keeping it.
    if (_orphanPhp && millis() - _orphanAtMs < COIN_ORPHAN_WINDOW_MS) {
      uint32_t peso = _orphanPhp;
      _orphanPhp = 0;
      addCoin(peso);
      _admin->logEvent("coin_orphan_claimed", "PHP " + String(peso) + " by " + mac);
    }
    _orphanPhp = 0;
    return "";
  }

  // Fills a status document for the reservation owning `token`.
  void status(const String& token, JsonDocument& out) {
    if (_reserved && token == _token) {
      out["state"] = "inserting";
      out["pesos"] = _pesos;
      out["minutes"] = _seconds / 60;
      out["dataMb"] = _unlimitedData ? 0 : (uint32_t)(_bytes / 1048576ULL);
      out["unmatchedPesos"] = _unmatchedPesos;
      uint32_t idle = millis() - _lastActivityMs;
      uint32_t limit = _seconds ? COIN_IDLE_TIMEOUT_MS : COIN_NO_COIN_TIMEOUT_MS;
      out["secondsLeft"] = idle < limit ? (limit - idle) / 1000 : 0;
      return;
    }
    if (_doneToken.length() && token == _doneToken && millis() - _doneAtMs < COIN_RESULT_KEEP_MS) {
      out["state"] = "done";
      out["code"] = _doneCode;
      out["extended"] = _doneExtended;
      return;
    }
    out["state"] = "none";
  }

  // Customer tapped Done. Returns "" and fills `code` on success.
  String done(const String& token, String& code, bool& extended) {
    if (_doneToken.length() && token == _doneToken && millis() - _doneAtMs < COIN_RESULT_KEEP_MS) {
      code = _doneCode;           // already finished (timeout beat the tap)
      extended = _doneExtended;
      return "";
    }
    if (!_reserved || token != _token) return "no_coin_session";
    if (!_seconds) {              // tapped Done without paying anything
      release();
      return "no_coins_inserted";
    }
    return finish(code, extended);
  }

  void cancel(const String& token) {
    if (_reserved && token == _token && !_seconds) release();
  }

  // ---- called from the main loop -------------------------------------

  // One debounced coin drop worth `peso`.
  void addCoin(uint32_t peso) {
    if (!_reserved) {
      _orphanPhp += peso;
      _orphanAtMs = millis();
      _admin->logEvent("coin_orphan", "PHP " + String(peso) + " inserted with no customer on Insert Coin");
      return;
    }
    _lastActivityMs = millis();
    RateProfile* p = _admin->profileByPeso(peso);
    if (!p) {
      // Unknown denomination: don't guess a rate. Shown to the customer
      // and logged so the admin can add a Rate Profile for it.
      _unmatchedPesos += peso;
      _admin->logEvent("coin_unmatched", "PHP " + String(peso) + " has no matching Rate Profile");
      return;
    }
    _pesos += peso;
    _seconds += p->minutes * 60UL;
    if (p->dataMb == 0) _unlimitedData = true;
    else _bytes += (uint64_t)p->dataMb * 1048576ULL;
    // Speed and pause window follow the biggest coin in the batch.
    if (p->pesoAmount >= _topPeso) {
      _topPeso = p->pesoAmount;
      _tier = p->speedProfile;
    }
    uint32_t window = p->validityMinutes ? p->validityMinutes : MAX_PAUSE_MINUTES;
    if (window > _pauseWindow) _pauseWindow = window;
  }

  void loop() {
    if (!_reserved) return;
    uint32_t idle = millis() - _lastActivityMs;
    if (!_seconds) {
      if (idle > COIN_NO_COIN_TIMEOUT_MS) release();
      return;
    }
    if (idle > COIN_IDLE_TIMEOUT_MS) {
      String code;
      bool extended;
      if (finish(code, extended).length()) {
        _lastActivityMs = millis(); // MikroTik unreachable - keep the credit, retry after another idle period
      }
    }
  }

  bool reserved() const { return _reserved; }

private:
  SessionManager* _sessions = nullptr;
  AdminAPI* _admin = nullptr;
  PPPoEManager* _pppoe = nullptr;
  QoSManager* _qos = nullptr;
  MikrotikAPI* _api = nullptr;
  TelegramNotifier* _telegram = nullptr;

  bool _reserved = false;
  int _relayGpio = -1;

  void setRelay(bool on) {
    if (_relayGpio < 0) return;   // "none": acceptor always powered
    digitalWrite(_relayGpio, (on == _admin->relayActiveHigh) ? HIGH : LOW);
  }
  String _token, _mac, _extendSession;
  uint32_t _lastActivityMs = 0;
  uint32_t _pesos = 0, _unmatchedPesos = 0, _seconds = 0, _topPeso = 0, _pauseWindow = 0;
  uint64_t _bytes = 0;
  bool _unlimitedData = false;
  String _tier = "1";

  uint32_t _orphanPhp = 0, _orphanAtMs = 0;

  String _doneToken, _doneCode;
  bool _doneExtended = false;
  uint32_t _doneAtMs = 0;

  void clearCredit() {
    _pesos = _unmatchedPesos = _seconds = _topPeso = _pauseWindow = 0;
    _bytes = 0;
    _unlimitedData = false;
    _tier = "1";
  }

  void release() {
    _reserved = false;
    _token = "";
    clearCredit();
    setRelay(false);
  }

  static String randomCode(uint8_t len) {
    static const char* alphabet = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    String s;
    for (uint8_t i = 0; i < len; i++) s += alphabet[RANDOM_REG32 % 32];
    return s;
  }

  // Turns the reservation's credit into internet. Returns "" on success.
  String finish(String& code, bool& extended) {
    uint64_t bytes = _unlimitedData ? UNLIMITED_BYTES : _bytes;
    extended = false;

    SessionEntry* existing = _extendSession.length() ? _sessions->findSession(_extendSession) : nullptr;
    if (existing && !existing->isSubscriber && existing->mode == "hotspot") {
      uint32_t newSeconds = existing->remainingSeconds + _seconds;
      uint64_t newBytes = (existing->remainingBytes == UNLIMITED_BYTES || bytes == UNLIMITED_BYTES)
                              ? UNLIMITED_BYTES : existing->remainingBytes + bytes;
      // A paused session has no MikroTik account right now (Pause removed
      // it) - just bank the credit; Resume re-creates the account with the
      // new totals. Re-creating it here would let the customer browse
      // while their clock is stopped.
      if (!existing->paused) {
        if (!_pppoe->provision(*_api, *_qos, existing->sessionId, "hotspot", existing->tier, newSeconds, newBytes)) {
          return "mikrotik_unreachable";
        }
        existing->lastBytesSeen = 0;
      }
      existing->remainingSeconds = newSeconds;
      existing->remainingBytes = newBytes;
      code = existing->sessionId;
      extended = true;
    } else {
      do { code = "ZX" + randomCode(6); } while (_sessions->findSession(code) || _admin->findVoucher(code));
      if (!_pppoe->provision(*_api, *_qos, code, "hotspot", _tier, _seconds, bytes)) {
        return "mikrotik_unreachable";
      }
      if (!_sessions->addSession(code, "hotspot", _tier, _seconds, bytes, false, _pauseWindow)) {
        _pppoe->deprovision(*_api, code, "hotspot");
        return "too_many_users_try_later";
      }
      _admin->usersToday++;
    }

    _admin->addCoinRevenue(0, _pesos);
    String what = "PHP " + String(_pesos) + " = " + String(_seconds / 60) + " min";
    _admin->logEvent(extended ? "coin_extend" : "coin", code + " " + what + " (" + _mac + ")");
    _telegram->queueMessage("Coin sale: " + what + (extended ? " (top-up)" : ""));

    _doneToken = _token;
    _doneCode = code;
    _doneExtended = extended;
    _doneAtMs = millis();
    release();
    return "";
  }
};

#endif // COIN_SLOT_H
