/*
 * box_store.h - what a box remembers (LittleFS /box.json)
 *
 * WiFi + main host come from the Setup Wizard; the vendo id and key from
 * pairing. The main unit's config (name, pins, rates...) is kept as the
 * raw JSON it sent, so each box type reads its own fields from it.
 * counterBase: message counters below it may already have been used -
 * after a reboot the box continues from there, so the main unit never
 * sees a counter go backwards (it would refuse it as a replay).
 * lastStopId: the last admin Stop already carried out (charging boxes).
 * Written to a temp file and renamed, so a power cut can't corrupt it.
 */
#ifndef BOX_STORE_H
#define BOX_STORE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "box_config.h"
#include "../zx_protocol.h"

struct BoxState {
  String wifiSsid;
  String wifiPass;
  String mainHost = BOX_DEFAULT_MAIN_HOST;
  String pairCode;          // from the wizard; cleared once paired
  uint8_t vendoId = 0;      // 0 = not paired
  uint8_t key[32];
  String name;
  uint32_t cfgVer = 0;
  uint32_t counterBase = 1;
  uint32_t lastStopId = 0;
  String configJson = "{}"; // the main unit's last config for this box

  bool configured() const { return wifiSsid.length() > 0; }
  bool paired() const { return vendoId != 0; }

  bool load() {
    File f = LittleFS.open(BOX_STATE_FILE, "r");
    if (!f) return false;
    DynamicJsonDocument d(2048);
    bool bad = (bool)deserializeJson(d, f);
    f.close();
    if (bad) return false;
    wifiSsid = d["wifiSsid"] | "";
    wifiPass = d["wifiPass"] | "";
    mainHost = d["mainHost"] | BOX_DEFAULT_MAIN_HOST;
    pairCode = d["pairCode"] | "";
    vendoId = d["vendoId"] | 0;
    String k = d["key"] | "";
    if (vendoId && !zx::fromHex(k.c_str(), key, 32)) vendoId = 0;
    name = d["name"] | "";
    cfgVer = d["cfgVer"] | 0UL;
    counterBase = d["counterBase"] | 1UL;
    lastStopId = d["lastStopId"] | 0UL;
    configJson = "";
    serializeJson(d["config"], configJson);
    if (configJson == "null" || !configJson.length()) configJson = "{}";
    return true;
  }

  bool save() {
    DynamicJsonDocument d(2048);
    d["wifiSsid"] = wifiSsid;
    d["wifiPass"] = wifiPass;
    d["mainHost"] = mainHost;
    d["pairCode"] = pairCode;
    d["vendoId"] = vendoId;
    if (vendoId) d["key"] = zx::hexOf(key, 32);
    d["name"] = name;
    d["cfgVer"] = cfgVer;
    d["counterBase"] = counterBase;
    d["lastStopId"] = lastStopId;
    DynamicJsonDocument cfg(1024);
    if (!deserializeJson(cfg, configJson)) d["config"] = cfg.as<JsonVariantConst>();
    File f = LittleFS.open(String(BOX_STATE_FILE) + ".tmp", "w");
    if (!f) return false;
    serializeJson(d, f);
    f.close();
    LittleFS.remove(BOX_STATE_FILE);
    return LittleFS.rename(String(BOX_STATE_FILE) + ".tmp", BOX_STATE_FILE);
  }

  // Config as sent by the main unit (pairing reply or /api/vendo/config).
  void applyConfig(JsonVariantConst c) {
    if (c.isNull()) return;
    name = c["name"] | name;
    cfgVer = c["cfgVer"] | cfgVer;
    configJson = "";
    serializeJson(c, configJson);
  }
};

#endif // BOX_STORE_H
