/*
 * box_link.h - a box's signed connection to the main unit
 *
 * Every request and reply is signed (common/zx_protocol.h). The message
 * counter "n" keeps increasing across reboots (reserved in blocks of
 * BOX_COUNTER_BLOCK in BoxState.counterBase), so the main unit never
 * mistakes a fresh message for a replay.
 */
#ifndef BOX_LINK_H
#define BOX_LINK_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include "box_config.h"
#include "box_store.h"
#include "../zx_protocol.h"

class BoxLink {
public:
  explicit BoxLink(BoxState& state) : _state(state) {}

  // Call once at boot, after state.load(): never reuse a counter from
  // before the reboot.
  void begin() {
    _counter = _state.counterBase;
    _state.counterBase = _counter + BOX_COUNTER_BLOCK;
    _state.save();
  }

  uint32_t nextCounter() {
    _counter++;
    if (_counter >= _state.counterBase) {        // reserve the next block before using it
      _state.counterBase = _counter + BOX_COUNTER_BLOCK;
      _state.save();
    }
    return _counter;
  }

  // Signed POST to the main unit. Returns the HTTP status, 0 when a 200
  // reply fails its signature check, or <0 on a transport error.
  int postSigned(const char* path, const JsonDocument& payload, const uint8_t key[32], JsonDocument& resp) {
    String body;
    serializeJson(payload, body);
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(BOX_HTTP_TIMEOUT_MS);
    if (!http.begin(client, "http://" + _state.mainHost + path)) return -1;
    http.addHeader("Content-Type", "application/json");
    http.addHeader(ZX_SIG_HEADER, zx::sign(key, body));
    const char* headers[] = {ZX_SIG_HEADER};
    http.collectHeaders(headers, 1);
    int code = http.POST(body);
    String text = code > 0 ? http.getString() : String();
    String sig = http.header(ZX_SIG_HEADER);
    http.end();
    if (code != 200) return code;
    if (!zx::verify(key, text, sig)) return 0;
    if (deserializeJson(resp, text)) return 0;
    return 200;
  }

  // Pairs with state.pairCode. On success stores the vendo id, key, name
  // and config, clears the code and returns true; `resp` keeps the reply
  // (e.g. lastCoinSeq) for the caller.
  bool pair(JsonDocument& resp) {
    uint8_t k0[32];
    zx::pairKey(_state.pairCode, k0);
    char nonce[17];
    for (int i = 0; i < 8; i++) snprintf(nonce + 2 * i, 3, "%02x", (unsigned)(RANDOM_REG32 & 0xFF));
    StaticJsonDocument<128> req;
    req["mac"] = WiFi.macAddress();
    req["nonce"] = nonce;
    int code = postSigned("/api/vendo/pair", req, k0, resp);
    if (code != 200 || String((const char*)(resp["nonce"] | "")) != nonce) {
      Serial.printf("Pairing: no (HTTP %d) - check the code in Admin > Vendos; retrying\n", code);
      return false;
    }
    uint8_t id = resp["vendoId"] | 0;
    if (!id) return false;
    _state.vendoId = id;
    zx::vendoKey(k0, id, WiFi.macAddress(), String(nonce), _state.key);
    _state.pairCode = "";
    _state.applyConfig(resp["config"]);
    _state.save();
    Serial.printf("Paired as vendo %u (%s)\n", id, _state.name.c_str());
    return true;
  }

private:
  BoxState& _state;
  uint32_t _counter = 0;
};

#endif // BOX_LINK_H
