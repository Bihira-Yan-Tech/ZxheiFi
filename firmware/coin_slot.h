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
 * Only one customer can hold a slot at a time; a second one gets
 * "coin_slot_busy" with the seconds left until it frees up.
 *
 * v2 - one slot per vendo (vendo_registry.h). Slot 0 is this unit's own
 * acceptor (local pins). Slots 1..MAX_SUB_VENDOS belong to sub vendos:
 * no local pins - a sub learns "relay on" from its poll (relayFor) and
 * reports coins over the signed protocol (addRemoteCoin, vendo_api.h).
 * Every reservation gets a unique id (rid). A sub tags each coin with the
 * rid it saw when the coin went in, so a coin delivered late (the sub
 * lost WiFi for a moment) reaches the customer who paid it - topping up
 * the session they already got - and never the next customer.
 * A coin nobody claims within COIN_ORPHAN_WINDOW_MS is still counted in
 * sales (the cash is in the box) and logged as coin_late_unclaimed.
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
#include "vendo_registry.h"

class CoinSlot {
public:
  void begin(SessionManager& sessions, AdminAPI& admin, PPPoEManager& pppoe,
             QoSManager& qos, MikrotikAPI& api, TelegramNotifier& telegram, VendoRegistry& vendos) {
    _sessions = &sessions;
    _admin = &admin;
    _pppoe = &pppoe;
    _qos = &qos;
    _api = &api;
    _telegram = &telegram;
    _vendos = &vendos;
    // Random start: a sub's coin queued before our reboot can't carry a
    // rid that happens to match a fresh reservation.
    _nextRid = (ZX_RANDOM32() & 0x3FFFFFFF) + 1;
    applyRelayPin();
  }

  // Settings > Coin Slot: relay pin and whether it switches on HIGH or LOW
  // (relay modules come both ways). Safe to call again after a change.
  void applyRelayPin() {
    if (_relayGpio >= 0) pinMode(_relayGpio, INPUT);   // release the old pin
    _relayGpio = coinPinGpio(_admin->relayPin);
    if (_relayGpio >= 0) pinMode(_relayGpio, OUTPUT);
    setRelay(_slots[0].reserved);
  }

  // ---- called from the HTTP handlers ---------------------------------

  // Returns "" on success (token filled in), or an error code:
  // vendo_unknown, vendo_offline, coin_slot_busy.
  String start(uint8_t vendo, const String& mac, const String& extendSession, String& token, uint32_t& waitSec) {
    if (vendo > MAX_SUB_VENDOS) return "vendo_unknown";
    Vendo* v = _vendos->find(vendo);
    if (!v || (vendo && !v->hasKey)) return "vendo_unknown";
    if (!_vendos->isOnline(*v)) return "vendo_offline";
    Slot& s = _slots[vendo];
    if (s.reserved && token.length() && token == s.token) { // same customer tapping again
      s.lastActivityMs = millis();
      return "";
    }
    if (s.reserved) {
      uint32_t idle = millis() - s.lastActivityMs;
      uint32_t limit = s.seconds ? COIN_IDLE_TIMEOUT_MS : COIN_NO_COIN_TIMEOUT_MS;
      waitSec = idle < limit ? (limit - idle) / 1000 + 1 : 1;
      return "coin_slot_busy";
    }
    clearCredit(s);
    s.reserved = true;
    s.token = randomCode(12);
    token = s.token;
    s.mac = mac;
    s.extendSession = extendSession;
    s.lastActivityMs = millis();
    s.rid = _nextRid++;
    if (!_nextRid) _nextRid = 1;
    if (vendo == 0) setRelay(true);

    // A coin dropped just before tapping Insert Coin was almost certainly
    // this customer's - credit it instead of keeping it. (loop() already
    // expired any older than COIN_ORPHAN_WINDOW_MS.)
    if (s.orphanPhp) {
      uint32_t peso = s.orphanPhp;
      s.orphanPhp = 0;
      credit(s, peso);
      _admin->logEvent("coin_orphan_claimed", "PHP " + String(peso) + " by " + mac);
    }
    return "";
  }

  // Fills a status document for the reservation owning `token`.
  void status(const String& token, JsonDocument& out) {
    int i = slotForToken(token);
    if (i >= 0) {
      Slot& s = _slots[i];
      out["state"] = "inserting";
      out["vendo"] = i;
      out["pesos"] = s.pesos;
      out["minutes"] = s.seconds / 60;
      out["dataMb"] = s.unlimitedData ? 0 : (uint32_t)(s.bytes / 1048576ULL);
      out["unmatchedPesos"] = s.unmatchedPesos;
      uint32_t idle = millis() - s.lastActivityMs;
      uint32_t limit = s.seconds ? COIN_IDLE_TIMEOUT_MS : COIN_NO_COIN_TIMEOUT_MS;
      out["secondsLeft"] = idle < limit ? (limit - idle) / 1000 : 0;
      return;
    }
    int d = slotForDoneToken(token);
    if (d >= 0) {
      out["state"] = "done";
      out["code"] = _slots[d].doneCode;
      out["extended"] = _slots[d].doneExtended;
      return;
    }
    out["state"] = "none";
  }

  // Customer tapped Done. Returns "" and fills `code` on success.
  String done(const String& token, String& code, bool& extended) {
    int d = slotForDoneToken(token);
    if (d >= 0) {               // already finished (timeout beat the tap)
      code = _slots[d].doneCode;
      extended = _slots[d].doneExtended;
      return "";
    }
    int i = slotForToken(token);
    if (i < 0) return "no_coin_session";
    if (!_slots[i].seconds) {   // tapped Done without paying anything
      release(i);
      return "no_coins_inserted";
    }
    return finish(i, code, extended);
  }

  void cancel(const String& token) {
    int i = slotForToken(token);
    if (i >= 0 && !_slots[i].seconds) release(i);
  }

  // ---- called from the main loop / vendo_api.h -----------------------

  // One debounced coin drop worth `peso` on vendo `vendo`'s acceptor
  // (0 = this unit's own). Box totals are the caller's job.
  void addCoin(uint8_t vendo, uint32_t peso) {
    if (vendo > MAX_SUB_VENDOS) return;
    Slot& s = _slots[vendo];
    if (!s.reserved) {
      orphan(vendo, peso);
      return;
    }
    s.lastActivityMs = millis();
    credit(s, peso);
  }

  // A coin reported by sub vendo `vendo`, tagged with the reservation id
  // the sub saw when it went in (0 = the slot wasn't reserved then).
  void addRemoteCoin(uint8_t vendo, uint32_t peso, uint32_t rid) {
    if (vendo == 0 || vendo > MAX_SUB_VENDOS) return;
    Slot& s = _slots[vendo];
    if (rid && s.reserved && rid == s.rid) {
      s.lastActivityMs = millis();
      credit(s, peso);
      return;
    }
    if (rid && rid == s.doneRid && s.doneCode.length() && millis() - s.doneAtMs < COIN_RESULT_KEEP_MS) {
      if (lateExtend(vendo, peso)) return;
    }
    orphan(vendo, peso);
  }

  // What a sub's poll is told: relay on while its slot is reserved.
  bool relayFor(uint8_t vendo, uint32_t& rid) {
    rid = 0;
    if (vendo > MAX_SUB_VENDOS) return false;
    Slot& s = _slots[vendo];
    if (s.reserved) rid = s.rid;
    return s.reserved;
  }

  // Vendo removed or re-paired: forget its reservation. An orphan still
  // waiting is money in the box - recorded, not dropped.
  void resetSlot(uint8_t vendo) {
    if (vendo == 0 || vendo > MAX_SUB_VENDOS) return;
    uint32_t orphanPhp = _slots[vendo].orphanPhp;
    _slots[vendo] = Slot();
    if (orphanPhp) recordUnclaimed(vendo, orphanPhp);
  }

  void loop() {
    for (uint8_t i = 0; i <= MAX_SUB_VENDOS; i++) {
      Slot& s = _slots[i];
      if (s.orphanPhp && millis() - s.orphanAtMs >= COIN_ORPHAN_WINDOW_MS) {
        uint32_t peso = s.orphanPhp;
        s.orphanPhp = 0;
        recordUnclaimed(i, peso);
      }
      if (!s.reserved) continue;
      uint32_t idle = millis() - s.lastActivityMs;
      if (!s.seconds) {
        if (idle > COIN_NO_COIN_TIMEOUT_MS) release(i);
        continue;
      }
      if (idle > COIN_IDLE_TIMEOUT_MS) {
        String code;
        bool extended;
        if (finish(i, code, extended).length()) {
          s.lastActivityMs = millis(); // MikroTik unreachable - keep the credit, retry after another idle period
        }
      }
    }
  }

private:
  struct Slot {
    bool reserved = false;
    String token, mac, extendSession;
    uint32_t lastActivityMs = 0;
    uint32_t rid = 0;
    uint32_t pesos = 0, unmatchedPesos = 0, seconds = 0, topPeso = 0, pauseWindow = 0;
    uint64_t bytes = 0;
    bool unlimitedData = false;
    String tier = "1";
    uint32_t orphanPhp = 0, orphanAtMs = 0;
    String doneToken, doneCode;
    bool doneExtended = false;
    uint32_t doneAtMs = 0, doneRid = 0;
  };

  SessionManager* _sessions = nullptr;
  AdminAPI* _admin = nullptr;
  PPPoEManager* _pppoe = nullptr;
  QoSManager* _qos = nullptr;
  MikrotikAPI* _api = nullptr;
  TelegramNotifier* _telegram = nullptr;
  VendoRegistry* _vendos = nullptr;

  Slot _slots[MAX_SUB_VENDOS + 1];
  uint32_t _nextRid = 1;
  int _relayGpio = -1;

  void setRelay(bool on) {
    if (_relayGpio < 0) return;   // "none": acceptor always powered
    digitalWrite(_relayGpio, (on == _admin->relayActiveHigh) ? HIGH : LOW);
  }

  int slotForToken(const String& token) {
    if (!token.length()) return -1;
    for (uint8_t i = 0; i <= MAX_SUB_VENDOS; i++) {
      if (_slots[i].reserved && _slots[i].token == token) return i;
    }
    return -1;
  }

  int slotForDoneToken(const String& token) {
    if (!token.length()) return -1;
    for (uint8_t i = 0; i <= MAX_SUB_VENDOS; i++) {
      Slot& s = _slots[i];
      if (s.doneToken == token && millis() - s.doneAtMs < COIN_RESULT_KEEP_MS) return i;
    }
    return -1;
  }

  String vendoName(uint8_t vendo) {
    Vendo* v = _vendos->find(vendo);
    return v ? v->name : String("vendo ") + vendo;
  }

  void clearCredit(Slot& s) {
    s.pesos = s.unmatchedPesos = s.seconds = s.topPeso = s.pauseWindow = 0;
    s.bytes = 0;
    s.unlimitedData = false;
    s.tier = "1";
  }

  void release(uint8_t i) {
    Slot& s = _slots[i];
    s.reserved = false;
    s.token = "";
    s.rid = 0;
    clearCredit(s);
    if (i == 0) setRelay(false);
  }

  // Adds one coin's Rate Profile to a reservation.
  void credit(Slot& s, uint32_t peso) {
    RateProfile* p = _admin->profileByPeso(peso);
    if (!p) {
      // Unknown denomination: don't guess a rate. Shown to the customer
      // and logged so the admin can add a Rate Profile for it.
      s.unmatchedPesos += peso;
      _admin->logEvent("coin_unmatched", "PHP " + String(peso) + " has no matching Rate Profile");
      return;
    }
    s.pesos += peso;
    s.seconds += p->minutes * 60UL;
    if (p->dataMb == 0) s.unlimitedData = true;
    else s.bytes += (uint64_t)p->dataMb * 1048576ULL;
    // Speed and pause window follow the biggest coin in the batch.
    if (p->pesoAmount >= s.topPeso) {
      s.topPeso = p->pesoAmount;
      s.tier = p->speedProfile;
    }
    uint32_t window = p->validityMinutes ? p->validityMinutes : MAX_PAUSE_MINUTES;
    if (window > s.pauseWindow) s.pauseWindow = window;
  }

  void orphan(uint8_t vendo, uint32_t peso) {
    Slot& s = _slots[vendo];
    s.orphanPhp += peso;
    s.orphanAtMs = millis();
    _admin->logEvent("coin_orphan", "PHP " + String(peso) + " inserted with no customer on Insert Coin" +
                     (vendo ? " (" + vendoName(vendo) + ")" : String("")));
  }

  void recordUnclaimed(uint8_t vendo, uint32_t peso) {
    _admin->addCoinRevenue(vendo, peso);
    _admin->logEvent("coin_late_unclaimed", "PHP " + String(peso) + " at " + vendoName(vendo) +
                     " was never claimed - counted in sales; refund the customer if they ask");
  }

  static String randomCode(uint8_t len) {
    static const char* alphabet = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    String s;
    for (uint8_t i = 0; i < len; i++) s += alphabet[ZX_RANDOM32() % 32];
    return s;
  }

  // A coin from a reservation that already finished tops up the session
  // it produced. False if that's not possible (caller keeps it as orphan).
  bool lateExtend(uint8_t vendo, uint32_t peso) {
    Slot& s = _slots[vendo];
    RateProfile* p = _admin->profileByPeso(peso);
    SessionEntry* e = _sessions->findSession(s.doneCode);
    if (!p || !e || e->isSubscriber || e->mode != "hotspot") return false;
    uint32_t newSeconds = e->remainingSeconds + p->minutes * 60UL;
    uint64_t add = p->dataMb == 0 ? UNLIMITED_BYTES : (uint64_t)p->dataMb * 1048576ULL;
    uint64_t newBytes = (e->remainingBytes == UNLIMITED_BYTES || add == UNLIMITED_BYTES)
                            ? UNLIMITED_BYTES : e->remainingBytes + add;
    if (!e->paused) {
      if (!_pppoe->provision(*_api, *_qos, e->sessionId, "hotspot", e->tier, newSeconds, newBytes)) return false;
      e->lastBytesSeen = 0;
    }
    e->remainingSeconds = newSeconds;
    e->remainingBytes = newBytes;
    _admin->addCoinRevenue(vendo, peso);
    _admin->logEvent("coin_late_extended", e->sessionId + " PHP " + String(peso) + " (" + vendoName(vendo) + ")");
    return true;
  }

  // Turns slot i's credit into internet. Returns "" on success.
  String finish(uint8_t i, String& code, bool& extended) {
    Slot& s = _slots[i];
    uint64_t bytes = s.unlimitedData ? UNLIMITED_BYTES : s.bytes;
    extended = false;

    SessionEntry* existing = s.extendSession.length() ? _sessions->findSession(s.extendSession) : nullptr;
    if (existing && !existing->isSubscriber && existing->mode == "hotspot") {
      uint32_t newSeconds = existing->remainingSeconds + s.seconds;
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
      if (!_pppoe->provision(*_api, *_qos, code, "hotspot", s.tier, s.seconds, bytes)) {
        return "mikrotik_unreachable";
      }
      if (!_sessions->addSession(code, "hotspot", s.tier, s.seconds, bytes, false, s.pauseWindow)) {
        _pppoe->deprovision(*_api, code, "hotspot");
        return "too_many_users_try_later";
      }
      _admin->usersToday++;
    }

    _admin->addCoinRevenue(i, s.pesos);
    String what = "PHP " + String(s.pesos) + " = " + String(s.seconds / 60) + " min";
    _admin->logEvent(extended ? "coin_extend" : "coin", code + " " + what + " (" + s.mac + ")" +
                     (i ? " @" + vendoName(i) : String("")));
    _telegram->queueMessage("Coin sale" + (i ? " (" + vendoName(i) + ")" : String("")) + ": " + what +
                            (extended ? " (top-up)" : ""));

    s.doneToken = s.token;
    s.doneCode = code;
    s.doneExtended = extended;
    s.doneAtMs = millis();
    s.doneRid = s.rid;
    release(i);
    return "";
  }
};

#endif // COIN_SLOT_H
