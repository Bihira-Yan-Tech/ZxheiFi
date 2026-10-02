/*
 * platform.h - the one place that knows which board we're on
 * ZXHEIFI - main unit firmware (v2): NodeMCU (ESP8266) or ESP32 DevKit
 *
 * Everything else includes this instead of ESP8266/ESP32-specific headers
 * and uses the names below, so one source tree builds for both boards:
 *   pio run -d firmware -e main_esp8266
 *   pio run -d firmware -e main_esp32     (then: python tools/merge_esp32.py)
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <Arduino.h>

#if defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
  #include <HTTPClient.h>
  #include <WiFiClientSecure.h>
  #include <ESPmDNS.h>
  #include <FS.h>
  #include <SPIFFS.h>
  #include <esp_system.h>
  using ZxWebServer = WebServer;
  using ZxSecureClient = WiFiClientSecure;
  #define ZX_WIFI_OPEN       WIFI_AUTH_OPEN
  #define ZX_MDNS_UPDATE()   do {} while (0)
  #define ZX_FS_BEGIN()      SPIFFS.begin(true)   // formats on first use
  #define ZX_RANDOM32()      esp_random()
  #define ZX_LED_ON          HIGH                 // DevKit blue LED on GPIO2 lights on HIGH
#else
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  #include <ESP8266HTTPClient.h>
  #include <WiFiClientSecureBearSSL.h>
  #include <ESP8266mDNS.h>
  #include <FS.h>
  using ZxWebServer = ESP8266WebServer;
  using ZxSecureClient = BearSSL::WiFiClientSecure;
  #define ZX_WIFI_OPEN       ENC_TYPE_NONE
  #define ZX_MDNS_UPDATE()   MDNS.update()
  #define ZX_FS_BEGIN()      SPIFFS.begin()
  #define ZX_RANDOM32()      RANDOM_REG32
  #define ZX_LED_ON          LOW                  // NodeMCU built-in LED lights on LOW
#endif

// Coin-slot / relay pins the Admin may choose (Settings > Coin Slot), as
// labels printed on the board. Boot-strapping and fixed-use pins are left
// out on purpose (NodeMCU: D0/D3/D4/D8; ESP32: GPIO0/2/5/12/15, 25 = buzzer).
#if defined(ESP32)
static const char* const ZX_PIN_LABELS[] = {"G13", "G14", "G26", "G27", "G32", "G33"};
static const int ZX_PIN_GPIOS[] = {13, 14, 26, 27, 32, 33};
#else
static const char* const ZX_PIN_LABELS[] = {"D1", "D2", "D5", "D6", "D7"};
static const int ZX_PIN_GPIOS[] = {5, 4, 14, 12, 13};
#endif
static const size_t ZX_PIN_COUNT = sizeof(ZX_PIN_GPIOS) / sizeof(ZX_PIN_GPIOS[0]);

// Label -> GPIO for this board, or -1.
static int zxPinGpio(const String& label) {
  for (size_t i = 0; i < ZX_PIN_COUNT; i++) {
    if (label == ZX_PIN_LABELS[i]) return ZX_PIN_GPIOS[i];
  }
  return -1;
}

#endif // PLATFORM_H
