/*
 * vendo_api.h - sub vendo protocol + Admin > Vendos endpoints
 * ZXHEIFI - NodeMCU ESP8266 Firmware (v2)
 *
 * Sub vendos (subvendo/) are separate NodeMCUs with a coin acceptor. They
 * only count coins and switch their relay; everything else - prices,
 * reservations, sessions, sales - happens here. Every request and reply
 * is signed (X-ZX-Sig, common/zx_protocol.h):
 *
 *   POST /api/vendo/pair    {mac, nonce}          signed with the pairing code's K0
 *   POST /api/vendo/poll    {v, n}                -> {n, relay, rid, fast, cfgVer, name}
 *   POST /api/vendo/coin    {v, n, seq, peso, rid} -> {n, ok[, dup]}
 *   POST /api/vendo/config  {v, n}                -> {n, cfgVer, name, coinPin, ...}
 *   POST /api/vendo/charge  {v, n, seq, peso, port, minutes} -> {n, ok[, dup]}   (Charging Stations)
 *
 * Charging Stations (v2 part 2, charging/) run their ports themselves - so
 * charging works with no WiFi - and report every sale with /charge. Their
 * polls carry {ports:[secs...], ackStop} and the reply carries admin Stops.
 *
 * "n" must keep increasing (replays are refused), "seq" dedupes coin
 * resends, and a coin's seq + box total are saved BEFORE it is credited
 * and acknowledged, so a power cut can neither lose nor double it.
 *
 * Admin (auth like every other admin endpoint; staff may view and mark
 * Collected, everything else is super-only):
 *   GET  /api/admin/vendos              board, limit, vendos[], pending codes
 *   GET  /api/admin/vendos/collections
 *   POST /api/admin/vendos/add          {name}      -> pairing code
 *   POST /api/admin/vendos/update       {id, name?, commissionPct?, coinPin?, relayPin?, relayActiveHigh?, pesosPerPulse?}
 *   POST /api/admin/vendos/collected    {id}
 *   POST /api/admin/vendos/remove       {id}
 *   POST /api/admin/vendos/repair       {id}        -> new pairing code
 *   POST /api/admin/vendos/stop         {id, port}  Charging Station: stop a port (staff too)
 *
 * tools/mock_server.py mirrors all of this; tools/vendo_test.py is the
 * contract test.
 */
#ifndef VENDO_API_H
#define VENDO_API_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include "config.h"
#include "zx_protocol.h"
#include "admin_api.h"
#include "telegram.h"
#include "vendo_registry.h"
#include "coin_slot.h"
#include "gui_handler.h"

class VendoAPI {
public:
  void begin(ESP8266WebServer& server, GUIHandler& gui, VendoRegistry& reg, CoinSlot& coin,
             AdminAPI& admin, TelegramNotifier& telegram) {
    _server = &server;
    _gui = &gui;
    _reg = &reg;
    _coin = &coin;
    _admin = &admin;
    _telegram = &telegram;

    _server->on("/api/vendo/pair", HTTP_POST, [this]() { handlePair(); });
    _server->on("/api/vendo/poll", HTTP_POST, [this]() { handlePoll(); });
    _server->on("/api/vendo/coin", HTTP_POST, [this]() { handleCoin(); });
    _server->on("/api/vendo/config", HTTP_POST, [this]() { handleConfig(); });
    _server->on("/api/vendo/charge", HTTP_POST, [this]() { handleCharge(); });

    _server->on("/api/admin/vendos", HTTP_GET, [this]() { handleList(); });
    _server->on("/api/admin/vendos/collections", HTTP_GET, [this]() { handleCollections(); });
    _server->on("/api/admin/vendos/add", HTTP_POST, [this]() { handleAdd(); });
    _server->on("/api/admin/vendos/update", HTTP_POST, [this]() { handleUpdate(); });
    _server->on("/api/admin/vendos/collected", HTTP_POST, [this]() { handleCollected(); });
    _server->on("/api/admin/vendos/remove", HTTP_POST, [this]() { handleRemove(); });
    _server->on("/api/admin/vendos/repair", HTTP_POST, [this]() { handleRepair(); });
    _server->on("/api/admin/vendos/stop", HTTP_POST, [this]() { handleStop(); });
  }

  // Offline alerts: a paired sub silent for ZX_OFFLINE_ALERT_MS is logged
  // and sent to Telegram once; "back online" goes out on its next message.
  void loop() {
    if (millis() - _lastAlertCheck < 5000) return;
    _lastAlertCheck = millis();
    for (auto& v : _reg->all()) {
      if (!v.id || !v.hasKey || !v.lastSeenMs || v.offlineAlerted) continue;
      if (millis() - v.lastSeenMs < ZX_OFFLINE_ALERT_MS) continue;
      v.offlineAlerted = true;
      _admin->logEvent("vendo_offline", v.name);
      _telegram->queueMessage("Vendo " + v.name + " offline for 5 min");
    }
  }

private:
  ESP8266WebServer* _server = nullptr;
  GUIHandler* _gui = nullptr;
  VendoRegistry* _reg = nullptr;
  CoinSlot* _coin = nullptr;
  AdminAPI* _admin = nullptr;
  TelegramNotifier* _telegram = nullptr;
  uint32_t _lastAlertCheck = 0;
  uint32_t _pairFailMs[ZX_PAIR_FAIL_LIMIT] = {0};
  uint8_t _pairFailIdx = 0;

  // ---- helpers -------------------------------------------------------

  void sendSigned(int code, const JsonDocument& doc, const uint8_t key[32]) {
    String body;
    serializeJson(doc, body);
    _server->sendHeader(ZX_SIG_HEADER, zx::sign(key, body));
    _server->send(code, "application/json", body);
  }

  bool pairRateLimited() {
    uint8_t recent = 0;
    for (uint8_t i = 0; i < ZX_PAIR_FAIL_LIMIT; i++) {
      if (_pairFailMs[i] && millis() - _pairFailMs[i] < ZX_PAIR_FAIL_WINDOW_MS) recent++;
    }
    return recent >= ZX_PAIR_FAIL_LIMIT;
  }

  void recordPairFailure() {
    _pairFailMs[_pairFailIdx] = millis() | 1;   // never 0 (= empty slot)
    _pairFailIdx = (_pairFailIdx + 1) % ZX_PAIR_FAIL_LIMIT;
  }

  // Signature + replay check for poll/coin/config. Replies and returns
  // nullptr on failure; nothing changes before the signature is verified.
  Vendo* authVendo(JsonDocument& in, uint32_t& n) {
    const String& body = _server->arg("plain");
    if (deserializeJson(in, body) || !in.is<JsonObject>()) {
      _gui->replyError(400, "bad_json");
      return nullptr;
    }
    int id = in["v"] | 0;
    Vendo* v = (id > 0 && id <= MAX_SUB_VENDOS) ? _reg->find((uint8_t)id) : nullptr;
    if (!v || !v->hasKey) {
      _gui->replyError(401, "unpaired");
      return nullptr;
    }
    if (!zx::verify(v->key, body, _server->header(ZX_SIG_HEADER))) {
      _gui->replyError(401, "bad_sig");
      return nullptr;
    }
    n = in["n"] | 0UL;
    if (!_reg->acceptCounter(*v, n)) {
      _gui->replyError(409, "replay");
      return nullptr;
    }
    if (v->offlineAlerted) {
      v->offlineAlerted = false;
      _admin->logEvent("vendo_online", v->name);
      _telegram->queueMessage("Vendo " + v->name + " is back online");
    }
    return v;
  }

  // What the box sees as its config version: a Charging Station also
  // refetches when Settings > Charging changes.
  uint32_t effectiveCfgVer(const Vendo& v) const {
    return v.cfgVer + (v.isCharging() ? _admin->chargeCfgVer : 0);
  }

  void fillConfig(const Vendo& v, JsonObject o) {
    o["cfgVer"] = effectiveCfgVer(v);
    o["name"] = v.name;
    o["type"] = v.type;
    o["coinPin"] = v.coinPin;
    o["relayPin"] = v.relayPin;
    o["relayActiveHigh"] = v.relayActiveHigh;
    o["pesosPerPulse"] = v.pesosPerPulse;
    if (v.isCharging()) {
      o["ports"] = v.ports;
      o["portActiveHigh"] = v.portActiveHigh;
      o["maxMinutes"] = _admin->chargeMaxMinutes;
      o["lang"] = _admin->chargeLang;
      JsonArray rates = o.createNestedArray("rates");
      for (auto& r : _admin->chargeRates) {
        JsonObject ro = rates.createNestedObject();
        ro["peso"] = r.peso;
        ro["minutes"] = r.minutes;
      }
    }
  }

  // Coin/relay pins a box may use: a Charging Station's D1/D2 are its I2C bus.
  static bool pinAllowed(const Vendo& v, const String& pin) {
    if (v.isCharging()) return pin == "D5" || pin == "D6" || pin == "D7";
    return coinPinGpio(pin) >= 0;
  }

  static String cleanName(String s) {
    s.trim();
    return (s.length() >= 1 && s.length() <= 32) ? s : String();
  }

  bool parseBody(JsonDocument& in) {
    if (deserializeJson(in, _server->arg("plain")) || !in.is<JsonObject>()) {
      _gui->replyError(400, "bad_json");
      return false;
    }
    return true;
  }

  // Reads {"id": n} for an existing vendo; replies 404 and returns nullptr otherwise.
  Vendo* vendoFromBody(JsonDocument& in) {
    int id = in["id"] | -1;
    Vendo* v = (id >= 0 && id <= MAX_SUB_VENDOS) ? _reg->find((uint8_t)id) : nullptr;
    if (!v) _gui->replyError(404, "vendo_unknown");
    return v;
  }

  void replyLimit(const String& err) {
    DynamicJsonDocument out(160);
    out["error"] = err;
    out["limit"] = MAX_SUB_VENDOS;
    out["board"] = BOARD_NAME;
    _gui->replyJson(409, out);
  }

  // ---- device protocol ----------------------------------------------

  void handlePair() {
    if (pairRateLimited()) { _gui->replyError(429, "too_many_attempts"); return; }
    const String& body = _server->arg("plain");
    DynamicJsonDocument in(256);
    if (deserializeJson(in, body) || !in.is<JsonObject>()) { _gui->replyError(400, "bad_json"); return; }
    String mac = in["mac"] | "";
    String nonce = in["nonce"] | "";
    if (mac.length() < 1 || mac.length() > 32 || nonce.length() < 16 || nonce.length() > 64) {
      _gui->replyError(400, "bad_request");
      return;
    }
    int idx = _reg->matchPending(body, _server->header(ZX_SIG_HEADER));
    if (idx < 0) {
      recordPairFailure();
      _gui->replyError(401, "bad_code");
      return;
    }
    uint8_t k0[32];
    Vendo* v = _reg->completePair(idx, mac, nonce, k0);
    if (!v) { _gui->replyError(409, "vendo_limit"); return; }
    _coin->resetSlot(v->id);
    _admin->logEvent("vendo_paired", v->name + " (" + mac + ")");
    DynamicJsonDocument out(1024);
    out["vendoId"] = v->id;
    out["name"] = v->name;
    out["nonce"] = nonce;
    out["lastCoinSeq"] = v->lastCoinSeq;
    fillConfig(*v, out.createNestedObject("config"));
    sendSigned(200, out, k0);
  }

  void handlePoll() {
    DynamicJsonDocument in(256);
    uint32_t n = 0;
    Vendo* v = authVendo(in, n);
    if (!v) return;
    if (v->isCharging()) {
      if (in["ports"].is<JsonArray>()) {
        uint8_t i = 0;
        for (JsonVariant p : in["ports"].as<JsonArray>()) {
          if (i >= 4) break;
          long secs = p | 0L;
          v->portSecs[i++] = secs > 0 ? (uint32_t)secs : 0;
        }
        while (i < 4) v->portSecs[i++] = 0;
        v->portSecsAtMs = millis() | 1;
      }
      uint32_t ack = in["ackStop"] | 0UL;
      for (size_t i = v->stops.size(); i-- > 0;) {
        if (v->stops[i].id <= ack) v->stops.erase(v->stops.begin() + i);
      }
      DynamicJsonDocument out(512);
      out["n"] = n;
      out["relay"] = false;
      out["rid"] = 0;
      out["fast"] = false;
      out["cfgVer"] = effectiveCfgVer(*v);
      out["name"] = v->name;
      JsonArray stops = out.createNestedArray("stop");
      for (auto& st : v->stops) {
        JsonObject o = stops.createNestedObject();
        o["id"] = st.id;
        o["port"] = st.port;
      }
      sendSigned(200, out, v->key);
      return;
    }
    uint32_t rid = 0;
    bool on = _coin->relayFor(v->id, rid);
    DynamicJsonDocument out(256);
    out["n"] = n;
    out["relay"] = on;
    out["rid"] = rid;
    out["fast"] = on;
    out["cfgVer"] = v->cfgVer;
    out["name"] = v->name;
    sendSigned(200, out, v->key);
  }

  void handleCoin() {
    DynamicJsonDocument in(192);
    uint32_t n = 0;
    Vendo* v = authVendo(in, n);
    if (!v) return;
    if (v->isCharging()) { _gui->replyError(400, "wrong_type"); return; }
    long seq = in["seq"] | 0L;
    long peso = in["peso"] | 0L;
    long rid = in["rid"] | 0L;
    if (seq < 1 || rid < 0 || peso < 1 || peso > 1000) { _gui->replyError(400, "bad_coin"); return; }
    DynamicJsonDocument out(96);
    out["n"] = n;
    out["ok"] = true;
    if ((uint32_t)seq <= v->lastCoinSeq) {
      out["dup"] = true;          // a resend - already credited
      sendSigned(200, out, v->key);
      return;
    }
    v->lastCoinSeq = (uint32_t)seq;
    v->boxTotal += (uint32_t)peso;
    _reg->save();                 // persisted before crediting + acking
    _coin->addRemoteCoin(v->id, (uint32_t)peso, (uint32_t)rid);
    sendSigned(200, out, v->key);
  }

  void handleConfig() {
    DynamicJsonDocument in(128);
    uint32_t n = 0;
    Vendo* v = authVendo(in, n);
    if (!v) return;
    DynamicJsonDocument out(1024);
    out["n"] = n;
    fillConfig(*v, out.as<JsonObject>());
    sendSigned(200, out, v->key);
  }

  // A Charging Station sale (the box already started the port). Saved
  // before it's acknowledged; `seq` shares the box's coin sequence.
  void handleCharge() {
    DynamicJsonDocument in(256);
    uint32_t n = 0;
    Vendo* v = authVendo(in, n);
    if (!v) return;
    if (!v->isCharging()) { _gui->replyError(400, "wrong_type"); return; }
    long seq = in["seq"] | 0L;
    long peso = in["peso"] | 0L;
    long port = in["port"] | -1L;
    long minutes = in["minutes"] | 0L;
    if (seq < 1 || peso < 1 || peso > 1000 || port < 0 || port > v->ports || minutes < 0 || minutes > 2880) {
      _gui->replyError(400, "bad_charge");
      return;
    }
    DynamicJsonDocument out(96);
    out["n"] = n;
    out["ok"] = true;
    if ((uint32_t)seq <= v->lastCoinSeq) {
      out["dup"] = true;
      sendSigned(200, out, v->key);
      return;
    }
    v->lastCoinSeq = (uint32_t)seq;
    v->boxTotal += (uint32_t)peso;
    _reg->save();                 // persisted before the sale is counted + acked
    _admin->addChargeRevenue(v->id, (uint32_t)peso);
    if (port) {
      String what = "PHP " + String(peso) + " = " + String(minutes) + " min, port " + String(port);
      _admin->logEvent("charge", v->name + ": " + what);
      _telegram->queueMessage("Charging sale (" + v->name + "): " + what);
    } else {
      _admin->logEvent("charge_unclaimed", v->name + ": PHP " + String(peso) +
                       " never assigned to a port - counted in sales");
    }
    sendSigned(200, out, v->key);
  }

  // ---- admin -----------------------------------------------------------

  void handleList() {
    AdminAccount* a = _gui->authAdmin();
    if (!a) return;
    DynamicJsonDocument out(640 + 480 * (MAX_SUB_VENDOS + 1));
    out["board"] = BOARD_NAME;
    out["limit"] = MAX_SUB_VENDOS;
    JsonArray arr = out.createNestedArray("vendos");
    for (auto& v : _reg->all()) {
      JsonObject o = arr.createNestedObject();
      o["id"] = v.id;
      o["name"] = v.name;
      o["online"] = _reg->isOnline(v);
      o["todayPeso"] = _admin->coinTodayFor(v.id);
      o["boxTotal"] = v.boxTotal;
      o["commissionPct"] = v.commissionPct;
      o["paired"] = v.id == 0 || v.hasKey;
      o["type"] = v.type;
      if (v.id) {
        o["lastSeenSec"] = v.lastSeenMs ? (long)((millis() - v.lastSeenMs) / 1000) : -1L;
        o["coinPin"] = v.coinPin;
        o["relayPin"] = v.relayPin;
        o["relayActiveHigh"] = v.relayActiveHigh;
        o["pesosPerPulse"] = v.pesosPerPulse;
        if (v.isCharging()) {
          o["ports"] = v.ports;
          o["portActiveHigh"] = v.portActiveHigh;
          JsonArray secs = o.createNestedArray("portSecs");
          for (uint8_t i = 0; i < v.ports; i++) secs.add(v.portSecs[i]);
          o["portSecsAgoSec"] = v.portSecsAtMs ? (long)((millis() - v.portSecsAtMs) / 1000) : -1L;
        }
      }
    }
    JsonArray pend = out.createNestedArray("pending");
    if (a->role == "super") {
      for (auto& p : _reg->pending()) {
        JsonObject o = pend.createNestedObject();
        o["name"] = p.name;
        o["code"] = p.code;
        o["targetId"] = p.targetId;
        uint32_t age = millis() - p.createdMs;
        o["expiresInSec"] = age < ZX_PAIR_CODE_TTL_MS ? (ZX_PAIR_CODE_TTL_MS - age) / 1000 : 0;
      }
    }
    _gui->replyJson(200, out);
  }

  void handleCollections() {
    if (!_gui->authAdmin()) return;
    _gui->streamBegin();
    _gui->streamRaw("[");
    bool first = true;
    auto& list = _reg->collections();
    for (size_t i = list.size(); i-- > 0;) {   // newest first
      CollectionEntry& e = list[i];
      StaticJsonDocument<256> o;
      o["vendoId"] = e.vendoId;
      o["name"] = e.name;
      o["amount"] = e.amount;
      o["admin"] = e.admin;
      o["at"] = e.at;
      _gui->streamJsonItem(o, first);
    }
    _gui->streamRaw("]");
    _gui->streamEnd();
  }

  void handleAdd() {
    if (!_gui->authAdmin(true)) return;
    DynamicJsonDocument in(192);
    if (!parseBody(in)) return;
    String name = cleanName(in["name"] | "");
    if (!name.length()) { _gui->replyError(400, "bad_name"); return; }
    String type = in["type"] | "wifi";
    if (type != "wifi" && type != "charging") { _gui->replyError(400, "bad_type"); return; }
    String code;
    String err = _reg->addPending(name, 0, code, type);
    if (err.length()) { replyLimit(err); return; }
    _admin->logEvent("vendo_pair_code", name);
    DynamicJsonDocument out(128);
    out["code"] = code;
    out["expiresInSec"] = ZX_PAIR_CODE_TTL_MS / 1000;
    _gui->replyJson(200, out);
  }

  void handleUpdate() {
    if (!_gui->authAdmin(true)) return;
    DynamicJsonDocument in(384);
    if (!parseBody(in)) return;
    Vendo* v = vendoFromBody(in);
    if (!v) return;

    // Validate everything first - a rejected request changes nothing.
    String name = v->name;
    if (in.containsKey("name")) {
      name = cleanName(in["name"] | "");
      if (!name.length()) { _gui->replyError(400, "bad_name"); return; }
    }
    long commission = v->commissionPct;
    if (in.containsKey("commissionPct")) {
      if (!in["commissionPct"].is<long>()) { _gui->replyError(400, "bad_commission"); return; }
      commission = in["commissionPct"].as<long>();
      if (commission < 0 || commission > 100) { _gui->replyError(400, "bad_commission"); return; }
    }
    String coinPin = v->coinPin, relayPin = v->relayPin;
    bool activeHigh = v->relayActiveHigh;
    long pulse = v->pesosPerPulse;
    long ports = v->ports;
    bool portHigh = v->portActiveHigh;
    if (v->id) {
      if (in.containsKey("coinPin")) coinPin = in["coinPin"] | "";
      if (in.containsKey("relayPin")) relayPin = in["relayPin"] | "";
      if (in.containsKey("relayActiveHigh")) {
        if (!in["relayActiveHigh"].is<bool>()) { _gui->replyError(400, "bad_relay_level"); return; }
        activeHigh = in["relayActiveHigh"].as<bool>();
      }
      if (in.containsKey("pesosPerPulse")) {
        if (!in["pesosPerPulse"].is<long>()) { _gui->replyError(400, "bad_pulse_value"); return; }
        pulse = in["pesosPerPulse"].as<long>();
      }
      if (v->isCharging()) {
        if (in.containsKey("ports")) {
          if (!in["ports"].is<long>()) { _gui->replyError(400, "bad_ports"); return; }
          ports = in["ports"].as<long>();
        }
        if (ports < 1 || ports > 4) { _gui->replyError(400, "bad_ports"); return; }
        if (in.containsKey("portActiveHigh")) {
          if (!in["portActiveHigh"].is<bool>()) { _gui->replyError(400, "bad_relay_level"); return; }
          portHigh = in["portActiveHigh"].as<bool>();
        }
      }
      if (!pinAllowed(*v, coinPin) || (relayPin != "none" && !pinAllowed(*v, relayPin))) {
        _gui->replyError(400, "invalid_pin");
        return;
      }
      if (coinPin == relayPin) { _gui->replyError(400, "coin_and_relay_same_pin"); return; }
      if (pulse < 1 || pulse > 100) { _gui->replyError(400, "bad_pulse_value"); return; }
    }

    bool cfgChanged = v->id && (name != v->name || coinPin != v->coinPin || relayPin != v->relayPin ||
                                activeHigh != v->relayActiveHigh || pulse != v->pesosPerPulse ||
                                ports != v->ports || portHigh != v->portActiveHigh);
    v->name = name;
    v->commissionPct = (uint8_t)commission;
    if (v->id) {
      v->coinPin = coinPin;
      v->relayPin = relayPin;
      v->relayActiveHigh = activeHigh;
      v->pesosPerPulse = (uint16_t)pulse;
      v->ports = (uint8_t)ports;
      v->portActiveHigh = portHigh;
      if (cfgChanged) v->cfgVer++;
    }
    _reg->save();
    DynamicJsonDocument out(64);
    out["ok"] = true;
    out["cfgVer"] = v->id ? v->cfgVer : 0;
    _gui->replyJson(200, out);
  }

  void handleCollected() {
    AdminAccount* a = _gui->authAdmin();
    if (!a) return;
    DynamicJsonDocument in(128);
    if (!parseBody(in)) return;
    Vendo* v = vendoFromBody(in);
    if (!v) return;
    String name = v->name;
    uint32_t amount = _reg->collect(v->id, a->username);
    _admin->logEvent("vendo_collected", name + " PHP " + String(amount) + " by " + a->username);
    DynamicJsonDocument out(64);
    out["collected"] = amount;
    _gui->replyJson(200, out);
  }

  void handleRemove() {
    if (!_gui->authAdmin(true)) return;
    DynamicJsonDocument in(128);
    if (!parseBody(in)) return;
    if ((in["id"] | -1) == 0) { _gui->replyError(400, "cannot_remove_main"); return; }
    Vendo* v = vendoFromBody(in);
    if (!v) return;
    uint8_t id = v->id;
    String name = v->name;
    _coin->resetSlot(id);
    _reg->remove(id);             // v is invalid from here on
    _admin->logEvent("vendo_removed", name);
    _gui->replyOk();
  }

  void handleRepair() {
    if (!_gui->authAdmin(true)) return;
    DynamicJsonDocument in(128);
    if (!parseBody(in)) return;
    if ((in["id"] | -1) == 0) { _gui->replyError(400, "cannot_repair_main"); return; }
    Vendo* v = vendoFromBody(in);
    if (!v) return;
    _reg->dropPendingFor(v->id);
    String code;
    String err = _reg->addPending(v->name, v->id, code, v->type);
    if (err.length()) { replyLimit(err); return; }
    _reg->invalidate(v->id);      // the old key stops working right away
    _coin->resetSlot(v->id);
    _admin->logEvent("vendo_repair", v->name);
    DynamicJsonDocument out(128);
    out["code"] = code;
    out["expiresInSec"] = ZX_PAIR_CODE_TTL_MS / 1000;
    _gui->replyJson(200, out);
  }

  // Charging Station: stop a port. Delivered in the box's next poll replies
  // until it acknowledges; the minutes the customer loses are logged.
  void handleStop() {
    AdminAccount* a = _gui->authAdmin();
    if (!a) return;
    DynamicJsonDocument in(128);
    if (!parseBody(in)) return;
    Vendo* v = vendoFromBody(in);
    if (!v) return;
    if (!v->isCharging()) { _gui->replyError(400, "not_charging"); return; }
    long port = in["port"] | 0L;
    if (port < 1 || port > v->ports) { _gui->replyError(400, "bad_port"); return; }
    if (v->stops.size() >= 16) v->stops.erase(v->stops.begin());   // the box has been offline a long time
    v->stops.push_back({++v->stopSeq, (uint8_t)port});
    _reg->save();                 // stopSeq must survive a reboot
    _admin->logEvent("charge_stop", v->name + " port " + String(port) + " stopped by " + a->username +
                     " (~" + String(v->portSecs[port - 1] / 60) + " min left)");
    _gui->replyOk();
  }
};

#endif // VENDO_API_H
