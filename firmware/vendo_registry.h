/*
 * vendo_registry.h - the coin boxes this main unit sells time from
 * ZXHEIFI - NodeMCU ESP8266 Firmware (v2)
 *
 * Vendo 0 is this unit's own coin acceptor ("Main", always present).
 * Vendos 1..MAX_SUB_VENDOS are sub vendos: separate NodeMCUs with a coin
 * acceptor that join the WiFi and talk to us over the signed protocol in
 * common/zx_protocol.h (endpoints in vendo_api.h). A sub is added with a
 * one-time pairing code from Admin > Vendos > Add Vendo; the per-vendo
 * key is derived on both sides from that code and never sent anywhere.
 *
 * Persisted in /vendos.json (keys as hex) and /collections.json. Pending
 * pairing codes live in RAM only (15 min, single use; lost on reboot).
 * _vendos reserves its full capacity up front, so Vendo* pointers stay
 * valid for as long as the vendo exists.
 */
#ifndef VENDO_REGISTRY_H
#define VENDO_REGISTRY_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <vector>
#include "config.h"
#include "admin_api.h"
#include "zx_protocol.h"

struct StopCmd {
  uint32_t id;
  uint8_t port;
};

struct Vendo {
  uint8_t id = 0;
  String name;
  String type = "wifi";       // "wifi" (coin box for hotspot time) or "charging" (Charging Station)
  String mac;                 // the sub's MAC, recorded at pairing
  uint8_t key[32];            // per-vendo HMAC key
  bool hasKey = false;        // false = removed pairing / waiting for a re-pair
  uint8_t commissionPct = 0;  // location host's share (report only)
  uint32_t boxTotal = 0;      // pesos in the coin box since the last "Collected"
  uint32_t lastCoinSeq = 0;   // highest coin seq credited - resends are ignored
  String coinPin = "D5";      // pushed to the sub (Main uses Settings > Coin Slot)
  String relayPin = "D7";
  bool relayActiveHigh = true;
  uint16_t pesosPerPulse = 1;
  uint16_t cfgVer = 1;        // bumped when name/pins change - the sub refetches
  uint32_t pairedAt = 0;
  // Charging Station only
  uint8_t ports = 4;          // 1-4
  bool portActiveHigh = false;
  uint32_t stopSeq = 0;       // persisted: stop ids must keep increasing across our reboots
  std::vector<StopCmd> stops; // admin Stops not yet acknowledged by the box (RAM)
  uint32_t portSecs[4] = {0, 0, 0, 0};   // last reported time left per port
  uint32_t portSecsAtMs = 0;
  bool isCharging() const { return type == "charging"; }
  // runtime only
  uint32_t lastSeenMs = 0;
  uint32_t lastCounter = 0;
  bool counterSeen = false;
  bool offlineAlerted = false;
};

struct PendingPair {
  String code;         // "XXXX-XXXX-XXXX"
  String name;
  uint8_t k0[32];
  uint8_t targetId;    // 0 = a new vendo, else a re-pair of that id
  uint32_t createdMs;
  String type;         // for a new vendo: "wifi" / "charging"
};

struct CollectionEntry {
  uint8_t vendoId;
  String name;
  uint32_t amount;
  String admin;
  uint32_t at;         // epoch seconds (0 if the clock wasn't set yet)
};

class VendoRegistry {
public:
  void begin() {
    load();
    loadCollections();
  }

  Vendo* find(uint8_t id) {
    for (auto& v : _vendos) if (v.id == id) return &v;
    return nullptr;
  }

  std::vector<Vendo>& all() { return _vendos; }

  uint8_t subCount() const {
    uint8_t n = 0;
    for (auto& v : _vendos) if (v.id) n++;
    return n;
  }

  bool isOnline(const Vendo& v) const {
    if (v.id == 0) return true;
    return v.hasKey && v.lastSeenMs && millis() - v.lastSeenMs < ZX_VENDO_OFFLINE_MS;
  }

  bool isOnline(uint8_t id) {
    Vendo* v = find(id);
    return v && isOnline(*v);
  }

  // ---- pairing -------------------------------------------------------------

  // Admin > Add Vendo (targetId 0) or Re-pair (targetId = that vendo).
  // Returns "" and fills `code`, or "too_many_pending" / "vendo_limit".
  String addPending(const String& name, uint8_t targetId, String& code, const String& type = "wifi") {
    expirePending();
    if (_pending.size() >= ZX_MAX_PENDING_PAIRS) return "too_many_pending";
    if (!targetId) {
      uint8_t newOnes = 0;
      for (auto& p : _pending) if (!p.targetId) newOnes++;
      if (subCount() + newOnes >= MAX_SUB_VENDOS) return "vendo_limit";
    }
    PendingPair p;
    p.code = newCode();
    p.name = name;
    p.targetId = targetId;
    p.createdMs = millis();
    p.type = type;
    zx::pairKey(p.code, p.k0);
    _pending.push_back(p);
    code = p.code;
    return "";
  }

  std::vector<PendingPair>& pending() {
    expirePending();
    return _pending;
  }

  void dropPendingFor(uint8_t targetId) {
    for (size_t i = _pending.size(); i-- > 0;) {
      if (_pending[i].targetId == targetId) _pending.erase(_pending.begin() + i);
    }
  }

  // Index of the pending code whose K0 signed `body`, or -1.
  int matchPending(const String& body, const String& sigHex) {
    expirePending();
    for (size_t i = 0; i < _pending.size(); i++) {
      if (zx::verify(_pending[i].k0, body, sigHex)) return (int)i;
    }
    return -1;
  }

  // Consumes pending[idx] and stores the vendo's key. k0Out gets the
  // bootstrap key (the pairing reply is signed with it). nullptr if every
  // id is taken (the code is consumed either way).
  Vendo* completePair(int idx, const String& mac, const String& nonce, uint8_t k0Out[32]) {
    if (idx < 0 || (size_t)idx >= _pending.size()) return nullptr;
    PendingPair p = _pending[idx];
    _pending.erase(_pending.begin() + idx);
    memcpy(k0Out, p.k0, 32);
    uint8_t id = p.targetId ? p.targetId : freeId();
    if (!id) return nullptr;
    Vendo* v = find(id);
    if (!v) {
      if (_vendos.size() >= MAX_SUB_VENDOS + 1) return nullptr;
      Vendo nv;
      nv.id = id;
      nv.name = p.name;
      nv.type = p.type == "charging" ? "charging" : "wifi";
      _vendos.push_back(nv);
      v = &_vendos.back();
    }
    v->mac = mac;
    zx::vendoKey(p.k0, id, mac, nonce, v->key);
    v->hasKey = true;
    v->counterSeen = false;
    v->lastCounter = 0;
    v->lastSeenMs = millis();
    v->offlineAlerted = false;
    v->pairedAt = (uint32_t)time(nullptr);
    save();
    return v;
  }

  // Replay guard: every message counter must be higher than the last one.
  // (After our own reboot the first counter seen is accepted - a replayed
  // poll then only fakes "online" briefly; coins are deduped by seq.)
  bool acceptCounter(Vendo& v, uint32_t n) {
    if (!n || (v.counterSeen && n <= v.lastCounter)) return false;
    v.lastCounter = n;
    v.counterSeen = true;
    v.lastSeenMs = millis();
    return true;
  }

  // ---- money ---------------------------------------------------------------

  void addToBox(uint8_t id, uint32_t peso) {
    Vendo* v = find(id);
    if (!v) return;
    v->boxTotal += peso;
    save();
  }

  uint32_t collect(uint8_t id, const String& admin) {
    Vendo* v = find(id);
    if (!v) return 0;
    uint32_t amount = v->boxTotal;
    v->boxTotal = 0;
    CollectionEntry e;
    e.vendoId = id;
    e.name = v->name;
    e.amount = amount;
    e.admin = admin;
    e.at = AdminAPI::clockSynced() ? (uint32_t)time(nullptr) : 0;
    _collections.push_back(e);
    while (_collections.size() > MAX_COLLECTIONS) _collections.erase(_collections.begin());
    save();
    saveCollections();
    return amount;
  }

  std::vector<CollectionEntry>& collections() { return _collections; }

  // ---- lifecycle -----------------------------------------------------------

  void invalidate(uint8_t id) {
    Vendo* v = find(id);
    if (!v || !id) return;
    v->hasKey = false;
    v->counterSeen = false;
    save();
  }

  void remove(uint8_t id) {
    if (!id) return;
    dropPendingFor(id);
    for (size_t i = 0; i < _vendos.size(); i++) {
      if (_vendos[i].id == id) {
        _vendos.erase(_vendos.begin() + i);
        break;
      }
    }
    save();
  }

  bool save() {
    return saveJsonArray(VENDOS_FILE, _vendos.size(), 768, [this](size_t i, JsonObject o) {
      Vendo& v = _vendos[i];
      o["id"] = v.id;
      o["name"] = v.name;
      o["commissionPct"] = v.commissionPct;
      o["boxTotal"] = v.boxTotal;
      if (v.id) {
        o["mac"] = v.mac;
        if (v.hasKey) o["key"] = zx::hexOf(v.key, 32);
        o["lastCoinSeq"] = v.lastCoinSeq;
        o["coinPin"] = v.coinPin;
        o["relayPin"] = v.relayPin;
        o["relayActiveHigh"] = v.relayActiveHigh;
        o["pesosPerPulse"] = v.pesosPerPulse;
        o["cfgVer"] = v.cfgVer;
        o["pairedAt"] = v.pairedAt;
        o["type"] = v.type;
        if (v.isCharging()) {
          o["ports"] = v.ports;
          o["portActiveHigh"] = v.portActiveHigh;
          o["stopSeq"] = v.stopSeq;
        }
      }
      return true;
    });
  }

private:
  std::vector<Vendo> _vendos;
  std::vector<PendingPair> _pending;
  std::vector<CollectionEntry> _collections;

  void load() {
    _vendos.clear();
    _vendos.reserve(MAX_SUB_VENDOS + 1);
    loadJsonArray(VENDOS_FILE, 768, [this](JsonObject o) {
      int id = o["id"] | -1;
      if (id < 0 || id > MAX_SUB_VENDOS || find((uint8_t)id) || _vendos.size() >= MAX_SUB_VENDOS + 1) return;
      Vendo v;
      v.id = (uint8_t)id;
      String name = o["name"] | "";
      v.name = name.length() ? name : (id ? "Vendo " + String(id) : String("Main"));
      v.commissionPct = o["commissionPct"] | 0;
      v.boxTotal = o["boxTotal"] | 0;
      if (id) {
        v.mac = o["mac"] | "";
        String key = o["key"] | "";
        v.hasKey = zx::fromHex(key.c_str(), v.key, 32);
        v.lastCoinSeq = o["lastCoinSeq"] | 0;
        v.coinPin = o["coinPin"] | "D5";
        v.relayPin = o["relayPin"] | "D7";
        v.relayActiveHigh = o["relayActiveHigh"] | true;
        v.pesosPerPulse = o["pesosPerPulse"] | 1;
        v.cfgVer = o["cfgVer"] | 1;
        v.pairedAt = o["pairedAt"] | 0;
        v.type = String((const char*)(o["type"] | "wifi")) == "charging" ? "charging" : "wifi";
        uint8_t ports = o["ports"] | 4;
        v.ports = ports >= 1 && ports <= 4 ? ports : 4;
        v.portActiveHigh = o["portActiveHigh"] | false;
        v.stopSeq = o["stopSeq"] | 0UL;
      }
      _vendos.push_back(v);
    });
    if (!find(0)) {
      Vendo main;
      main.id = 0;
      main.name = "Main";
      _vendos.insert(_vendos.begin(), main);
    }
  }

  void loadCollections() {
    _collections.clear();
    loadJsonArray(COLLECTIONS_FILE, 256, [this](JsonObject o) {
      CollectionEntry e;
      e.vendoId = o["vendoId"] | 0;
      e.name = o["name"] | "";
      e.amount = o["amount"] | 0;
      e.admin = o["admin"] | "";
      e.at = o["at"] | 0;
      _collections.push_back(e);
      while (_collections.size() > MAX_COLLECTIONS) _collections.erase(_collections.begin());
    });
  }

  void saveCollections() {
    saveJsonArray(COLLECTIONS_FILE, _collections.size(), 256, [this](size_t i, JsonObject o) {
      CollectionEntry& e = _collections[i];
      o["vendoId"] = e.vendoId;
      o["name"] = e.name;
      o["amount"] = e.amount;
      o["admin"] = e.admin;
      o["at"] = e.at;
      return true;
    });
  }

  void expirePending() {
    for (size_t i = _pending.size(); i-- > 0;) {
      if (millis() - _pending[i].createdMs >= ZX_PAIR_CODE_TTL_MS) _pending.erase(_pending.begin() + i);
    }
  }

  // Smallest id not used by a vendo or reserved by a pending re-pair.
  uint8_t freeId() {
    for (uint8_t id = 1; id <= MAX_SUB_VENDOS; id++) {
      if (find(id)) continue;
      bool reserved = false;
      for (auto& p : _pending) if (p.targetId == id) reserved = true;
      if (!reserved) return id;
    }
    return 0;
  }

  static String newCode() {
    static const char* alphabet = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    String s;
    for (uint8_t i = 0; i < 12; i++) {
      if (i && i % 4 == 0) s += '-';
      s += alphabet[ZX_RANDOM32() % 32];
    }
    return s;
  }
};

#endif // VENDO_REGISTRY_H
