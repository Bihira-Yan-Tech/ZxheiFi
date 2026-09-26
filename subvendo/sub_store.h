/*
 * sub_store.h - what a sub vendo remembers (LittleFS /sub.json)
 * ZXHEIFI Sub Vendo firmware (v2)
 *
 * WiFi + main host come from the Setup Wizard; the vendo id, key and
 * pins from pairing (the main unit pushes pin changes later via cfgVer).
 * counterBase: message counters below it may already have been used -
 * after a reboot the sub continues from there, so the main unit never
 * sees a counter go backwards (it would refuse it as a replay).
 * Written to a temp file and renamed, so a power cut can't corrupt it.
 */
#ifndef SUB_STORE_H
#define SUB_STORE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "sub_config.h"
#include "zx_protocol.h"

struct SubState {
  String wifiSsid;
  String wifiPass;
  String mainHost = SUB_DEFAULT_MAIN_HOST;
  String pairCode;          // from the wizard; cleared once paired
  uint8_t vendoId = 0;      // 0 = not paired
  uint8_t key[32];
  String name;
  String coinPin = "D5";
  String relayPin = "D7";
  bool relayActiveHigh = true;
  uint16_t pesosPerPulse = 1;
  uint16_t cfgVer = 0;
  uint32_t counterBase = 1;

  bool configured() const { return wifiSsid.length() > 0; }
  bool paired() const { return vendoId != 0; }

  bool load() {
    File f = LittleFS.open(SUB_STATE_FILE, "r");
    if (!f) return false;
    DynamicJsonDocument d(1024);
    bool bad = (bool)deserializeJson(d, f);
    f.close();
    if (bad) return false;
    wifiSsid = d["wifiSsid"] | "";
    wifiPass = d["wifiPass"] | "";
    mainHost = d["mainHost"] | SUB_DEFAULT_MAIN_HOST;
    pairCode = d["pairCode"] | "";
    vendoId = d["vendoId"] | 0;
    String k = d["key"] | "";
    if (vendoId && !zx::fromHex(k.c_str(), key, 32)) vendoId = 0;
    name = d["name"] | "";
    coinPin = d["coinPin"] | "D5";
    relayPin = d["relayPin"] | "D7";
    relayActiveHigh = d["relayActiveHigh"] | true;
    pesosPerPulse = d["pesosPerPulse"] | 1;
    cfgVer = d["cfgVer"] | 0;
    counterBase = d["counterBase"] | 1UL;
    return true;
  }

  bool save() {
    DynamicJsonDocument d(1024);
    d["wifiSsid"] = wifiSsid;
    d["wifiPass"] = wifiPass;
    d["mainHost"] = mainHost;
    d["pairCode"] = pairCode;
    d["vendoId"] = vendoId;
    if (vendoId) d["key"] = zx::hexOf(key, 32);
    d["name"] = name;
    d["coinPin"] = coinPin;
    d["relayPin"] = relayPin;
    d["relayActiveHigh"] = relayActiveHigh;
    d["pesosPerPulse"] = pesosPerPulse;
    d["cfgVer"] = cfgVer;
    d["counterBase"] = counterBase;
    File f = LittleFS.open(String(SUB_STATE_FILE) + ".tmp", "w");
    if (!f) return false;
    serializeJson(d, f);
    f.close();
    LittleFS.remove(SUB_STATE_FILE);
    return LittleFS.rename(String(SUB_STATE_FILE) + ".tmp", SUB_STATE_FILE);
  }

  // Name + pins as sent by the main unit (pairing reply or /api/vendo/config).
  void applyConfig(JsonVariantConst c) {
    if (c.isNull()) return;
    name = c["name"] | name;
    String coin = c["coinPin"] | coinPin;
    String relay = c["relayPin"] | relayPin;
    if (subPinGpio(coin) >= 0) coinPin = coin;
    if (relay == "none" || subPinGpio(relay) >= 0) relayPin = relay;
    relayActiveHigh = c["relayActiveHigh"] | relayActiveHigh;
    uint16_t pulse = c["pesosPerPulse"] | pesosPerPulse;
    if (pulse >= 1 && pulse <= 100) pesosPerPulse = pulse;
    cfgVer = c["cfgVer"] | cfgVer;
  }
};

#endif // SUB_STORE_H
