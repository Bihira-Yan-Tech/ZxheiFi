/*
 * network_config.h - Persisted WiFi/MikroTik/initial-admin credentials
 * ZXHEIFI - NodeMCU ESP8266 Firmware
 *
 * Backs the Setup Wizard (setup_mode.h): whatever an installer enters into
 * the wizard's form gets saved here, and every boot after that reads it
 * instead of the compile-time WIFI_SSID/WIFI_PASSWORD/MIKROTIK_API_*
 * constants in config.h. A device that's never run the wizard (dev/testing,
 * or firmware flashed before this existed) has no NETWORK_CONFIG_FILE, so
 * NetworkConfig::load() returns false and every caller falls back to the
 * config.h constants - no migration needed, same zero-migration pattern
 * used elsewhere in this project (see AdminAPI's salted-password work).
 */
#ifndef NETWORK_CONFIG_H
#define NETWORK_CONFIG_H

#include <Arduino.h>
#include "platform.h"
#include <ArduinoJson.h>
#include "config.h"

class NetworkConfig {
public:
  String wifiSsid;
  String wifiPassword;
  String mikrotikHost;
  String mikrotikApiUser;
  String mikrotikApiPass;
  // Only read once, by AdminAPI::begin() seeding the first super admin on
  // a device with no admins.json yet - not re-read after that, but left
  // in the file rather than scrubbed out (same trust boundary as the
  // rest of this device's local SPIFFS storage).
  String initialAdminPassword;

  static bool exists() { return SPIFFS.exists(NETWORK_CONFIG_FILE); }

  // Returns false (leaving every field empty) if the wizard has never
  // run - callers must fall back to config.h's constants in that case.
  bool load() {
    if (!SPIFFS.exists(NETWORK_CONFIG_FILE)) return false;
    File f = SPIFFS.open(NETWORK_CONFIG_FILE, "r");
    if (!f) return false;
    DynamicJsonDocument doc(768);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return false;

    wifiSsid = doc["wifiSsid"].as<String>();
    wifiPassword = doc["wifiPassword"].as<String>();
    mikrotikHost = doc["mikrotikHost"].as<String>();
    mikrotikApiUser = doc["mikrotikApiUser"].as<String>();
    mikrotikApiPass = doc["mikrotikApiPass"].as<String>();
    initialAdminPassword = doc["initialAdminPassword"].as<String>();
    return true;
  }

  bool save() {
    DynamicJsonDocument doc(768);
    doc["wifiSsid"] = wifiSsid;
    doc["wifiPassword"] = wifiPassword;
    doc["mikrotikHost"] = mikrotikHost;
    doc["mikrotikApiUser"] = mikrotikApiUser;
    doc["mikrotikApiPass"] = mikrotikApiPass;
    doc["initialAdminPassword"] = initialAdminPassword;
    File f = SPIFFS.open(NETWORK_CONFIG_FILE, "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    return true;
  }
};

#endif // NETWORK_CONFIG_H
