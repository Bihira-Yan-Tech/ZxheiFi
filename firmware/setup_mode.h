/*
 * setup_mode.h - First-boot captive setup wizard
 * ZXHEIFI - NodeMCU ESP8266 Firmware
 *
 * Runs INSTEAD of normal operation (never alongside it) when no
 * network_config.h::NetworkConfig has been saved yet, or when the
 * onboard FLASH button (PIN_SETUP_BUTTON, GPIO0 - already present on
 * most NodeMCU dev boards, no extra wiring needed) is pressed in the
 * first few seconds after boot, while the blue LED blinks fast.
 * Broadcasts its own WiFi AP, answers every DNS query with its own IP
 * (the standard "captive portal" trick - any URL a phone/laptop tries
 * to open lands on the setup form instead), and saves whatever the
 * installer submits to network.json before rebooting into normal
 * operation. This exists so a shop owner never has to open the Arduino
 * IDE/PlatformIO or edit config.h just to point the device at their own
 * WiFi/MikroTik - see docs/12-setup-wizard.md.
 */
#ifndef SETUP_MODE_H
#define SETUP_MODE_H

#include <Arduino.h>
#include "platform.h"
#include <DNSServer.h>
#include "config.h"
#include "network_config.h"

class SetupModeManager {
public:
  // True if the installer should be dropped into Setup Mode this boot:
  // no saved config yet, or FLASH pressed during the short window right
  // after boot (the blue LED blinks fast while it's open).
  //
  // It used to read the button once at the very start of setup(). GPIO0
  // held LOW at power-on puts the ESP8266 into its ROM flashing mode, so
  // "hold FLASH while powering on" never reached this code at all - and
  // releasing it before setup() ran meant the read always saw HIGH.
  static bool shouldEnter() {
    pinMode(PIN_SETUP_BUTTON, INPUT_PULLUP);
    if (!NetworkConfig::exists()) return true;

    pinMode(PIN_LED_STATUS, OUTPUT);
    Serial.printf("Press FLASH within %us to open Setup Mode...\n", SETUP_BUTTON_WINDOW_MS / 1000);
    uint32_t start = millis();
    while (millis() - start < SETUP_BUTTON_WINDOW_MS) {
      digitalWrite(PIN_LED_STATUS, ((millis() - start) / 100) % 2); // fast blink
      if (digitalRead(PIN_SETUP_BUTTON) == LOW) {
        delay(50); // debounce
        if (digitalRead(PIN_SETUP_BUTTON) == LOW) {
          digitalWrite(PIN_LED_STATUS, ZX_LED_ON); // solid on
          return true;
        }
      }
      delay(10);
    }
    digitalWrite(PIN_LED_STATUS, !ZX_LED_ON); // off
    return false;
  }

  void begin(ZxWebServer& server) {
    _server = &server;

    IPAddress apIP(192, 168, 4, 1);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(SETUP_AP_SSID);

    _dnsServer.start(53, "*", apIP); // every domain resolves to us

    _server->on("/", HTTP_GET, [this]() { handleForm(""); });
    _server->on("/save", HTTP_POST, [this]() { handleSave(); });
    // Captive-portal detection on phones/laptops probes assorted URLs
    // (generate_204, hotspot-detect.html, ncsi.txt, etc.) expecting
    // either a redirect or specific content to decide whether to pop
    // up a "sign in to network" prompt - answering all of them with the
    // same form is simpler and more reliable than trying to match each
    // OS's exact probe behavior.
    _server->onNotFound([this]() { handleForm(""); });
    _server->begin();

    Serial.println("=== SETUP MODE ===");
    Serial.printf("Connect to WiFi \"%s\", then open any website\n", SETUP_AP_SSID);
    Serial.println("(or browse to http://192.168.4.1/ directly)");
  }

  void loop() {
    _dnsServer.processNextRequest();
    _server->handleClient();
  }

private:
  ZxWebServer* _server = nullptr;
  DNSServer _dnsServer;

  void handleForm(const String& error) {
    String html = F(
      "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
      "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
      "<title>ZxheiFi Setup</title><style>"
      "body{font-family:sans-serif;background:#0f0f23;color:#e8e8ec;"
      "display:flex;justify-content:center;padding:24px 16px;}"
      ".card{background:#1a1a24;border-radius:16px;padding:24px;max-width:380px;width:100%;}"
      "h1{font-size:20px;margin:0 0 4px;color:#2dd4c9;}"
      "p.sub{font-size:13px;color:#888;margin:0 0 20px;}"
      "label{display:block;font-size:13px;color:#aaa;margin:14px 0 4px;}"
      "input{width:100%;padding:10px 12px;border-radius:8px;border:1px solid #333;"
      "background:#0f0f23;color:#fff;font-size:15px;box-sizing:border-box;}"
      "button{width:100%;margin-top:20px;padding:12px;border:none;border-radius:8px;"
      "background:#2dd4c9;color:#000;font-weight:600;font-size:15px;cursor:pointer;}"
      ".err{background:#e1705522;border:1px solid #e17055;color:#e17055;"
      "padding:10px 12px;border-radius:8px;font-size:13px;margin-bottom:14px;}"
      "</style></head><body><div class='card'>"
      "<h1>ZxheiFi Setup</h1>"
      "<p class='sub'>One-time setup - saved on this device, no reflashing needed.</p>"
    );
    if (error.length()) {
      html += "<div class='err'>" + error + "</div>";
    }
    html += F(
      "<form method='POST' action='/save'>"
      "<label>WiFi SSID (the MikroTik's hotspot WiFi)</label>"
      "<input name='wifiSsid' required autocapitalize='none' autocorrect='off' spellcheck='false'>"
      "<label>WiFi Password</label>"
      "<input name='wifiPassword' type='password'>"
      "<label>MikroTik IP Address</label>"
      "<input name='mikrotikHost' value='10.0.0.1' required>"
      "<label>MikroTik API Username</label>"
      "<input name='mikrotikApiUser' value='zxheifi-api' required>"
      "<label>MikroTik API Password</label>"
      "<input name='mikrotikApiPass' required autocapitalize='none' autocorrect='off' spellcheck='false'>"
      "<label>Admin Dashboard Password (for this device's own Settings)</label>"
      "<input name='initialAdminPassword' type='password' required minlength='4'>"
      "<button type='submit'>Save &amp; Restart</button>"
      "</form></div></body></html>"
    );
    _server->send(200, "text/html", html);
  }

  void handleSave() {
    NetworkConfig cfg;
    cfg.wifiSsid = _server->arg("wifiSsid");
    cfg.wifiPassword = _server->arg("wifiPassword");
    cfg.mikrotikHost = _server->arg("mikrotikHost");
    cfg.mikrotikApiUser = _server->arg("mikrotikApiUser");
    cfg.mikrotikApiPass = _server->arg("mikrotikApiPass");
    cfg.initialAdminPassword = _server->arg("initialAdminPassword");

    if (!cfg.wifiSsid.length() || !cfg.mikrotikHost.length() ||
        !cfg.mikrotikApiUser.length() || !cfg.mikrotikApiPass.length() ||
        cfg.initialAdminPassword.length() < 4) {
      handleForm("Please fill in every field (admin password needs at least 4 characters).");
      return;
    }

    if (!cfg.save()) {
      handleForm("Could not save to flash storage - please try again.");
      return;
    }

    _server->send(200, "text/html",
      "<!DOCTYPE html><html><body style='font-family:sans-serif;background:#0f0f23;"
      "color:#e8e8ec;text-align:center;padding-top:80px;'>"
      "<h1 style='color:#2dd4c9;'>Saved!</h1>"
      "<p>Restarting and connecting to your WiFi now...</p></body></html>");
    delay(500);
    ESP.restart();
  }
};

#endif // SETUP_MODE_H
