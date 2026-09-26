/*
 * sub_setup.h - Sub Vendo Setup Wizard (phone, captive portal)
 * ZXHEIFI Sub Vendo firmware (v2)
 *
 * Opens when the sub has no WiFi saved yet, or when the FLASH button is
 * pressed while the blue LED blinks fast right after power-on. Broadcasts
 * "ZxheiFi-Sub-Setup"; every web address shows the form. It asks for the
 * WiFi of the AP at this spot, the main unit's address and the pairing
 * code from Admin > Vendos > Add Vendo (leave it blank to keep an
 * existing pairing, e.g. when only the WiFi changed).
 */
#ifndef SUB_SETUP_H
#define SUB_SETUP_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include "sub_config.h"
#include "sub_store.h"
#include "zx_protocol.h"

class SubSetup {
public:
  // FLASH pressed during the fast-blink window after boot. (GPIO0 held at
  // power-on would start the ROM flasher instead - hence the window.)
  static bool buttonPressedInWindow() {
    pinMode(PIN_SETUP_BUTTON, INPUT_PULLUP);
    pinMode(PIN_LED_STATUS, OUTPUT);
    Serial.printf("Press FLASH within %us to open the Setup Wizard...\n", SETUP_BUTTON_WINDOW_MS / 1000);
    uint32_t start = millis();
    while (millis() - start < SETUP_BUTTON_WINDOW_MS) {
      digitalWrite(PIN_LED_STATUS, ((millis() - start) / 100) % 2);
      if (digitalRead(PIN_SETUP_BUTTON) == LOW) {
        delay(50);
        if (digitalRead(PIN_SETUP_BUTTON) == LOW) return true;
      }
      delay(10);
    }
    digitalWrite(PIN_LED_STATUS, HIGH);
    return false;
  }

  void begin(ESP8266WebServer& server, SubState& state) {
    _server = &server;
    _state = &state;
    IPAddress apIP(192, 168, 4, 1);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(SUB_SETUP_AP_SSID);
    _dns.start(53, "*", apIP);
    _server->on("/", HTTP_GET, [this]() { handleForm(""); });
    _server->on("/save", HTTP_POST, [this]() { handleSave(); });
    _server->onNotFound([this]() { handleForm(""); });
    _server->begin();
    digitalWrite(PIN_LED_STATUS, LOW);   // solid while the wizard is open
    Serial.printf("=== SUB VENDO SETUP ===\nConnect to WiFi \"%s\" and open any website (or http://192.168.4.1)\n",
                  SUB_SETUP_AP_SSID);
  }

  void loop() {
    _dns.processNextRequest();
    _server->handleClient();
  }

private:
  ESP8266WebServer* _server = nullptr;
  SubState* _state = nullptr;
  DNSServer _dns;

  static String attr(const String& s) {
    String out;
    for (size_t i = 0; i < s.length(); i++) {
      char c = s[i];
      if (c == '&') out += "&amp;";
      else if (c == '<') out += "&lt;";
      else if (c == '>') out += "&gt;";
      else if (c == '"') out += "&quot;";
      else if (c == '\'') out += "&#39;";
      else out += c;
    }
    return out;
  }

  void handleForm(const String& error) {
    String html = F(
      "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
      "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
      "<title>ZxheiFi Sub Vendo Setup</title><style>"
      "body{font-family:sans-serif;background:#0f0f23;color:#e8e8ec;display:flex;justify-content:center;padding:24px 16px;}"
      ".card{background:#1a1a24;border-radius:16px;padding:24px;max-width:380px;width:100%;}"
      "h1{font-size:20px;margin:0 0 4px;color:#2dd4c9;}p.sub{font-size:13px;color:#888;margin:0 0 16px;}"
      "label{display:block;font-size:13px;color:#aaa;margin:14px 0 4px;}"
      "input{width:100%;padding:10px 12px;border-radius:8px;border:1px solid #333;background:#0f0f23;color:#fff;"
      "font-size:15px;box-sizing:border-box;}"
      "button{width:100%;margin-top:20px;padding:12px;border:none;border-radius:8px;background:#2dd4c9;color:#000;"
      "font-weight:600;font-size:15px;}"
      ".err{background:#e1705522;border:1px solid #e17055;color:#e17055;padding:10px 12px;border-radius:8px;"
      "font-size:13px;margin-bottom:14px;}.ok{color:#34d399;font-size:13px;}"
      "</style></head><body><div class='card'><h1>ZxheiFi Sub Vendo</h1>"
    );
    html += "<p class='sub'>Firmware " SUB_FIRMWARE_VERSION " &middot; MAC " + WiFi.macAddress() + "</p>";
    if (_state->paired()) {
      html += "<p class='ok'>Paired as vendo " + String(_state->vendoId) + " (" + attr(_state->name) +
              "). Leave the pairing code blank to keep it.</p>";
    }
    if (error.length()) html += "<div class='err'>" + attr(error) + "</div>";
    html += F("<form method='POST' action='/save'>"
              "<label>WiFi name (the AP at this spot)</label>"
              "<input name='wifiSsid' required autocapitalize='none' autocorrect='off' spellcheck='false' value='");
    html += attr(_state->wifiSsid);
    html += F("'><label>WiFi password (blank for open WiFi)</label><input name='wifiPassword' type='password'>"
              "<label>Main unit address</label><input name='mainHost' required autocapitalize='none' value='");
    html += attr(_state->mainHost);
    html += F("'><label>Pairing code (Admin &gt; Vendos &gt; Add Vendo)</label>"
              "<input name='pairCode' placeholder='XXXX-XXXX-XXXX' autocapitalize='characters' autocorrect='off'>"
              "<button type='submit'>Save &amp; Restart</button></form></div></body></html>");
    _server->send(200, "text/html", html);
  }

  void handleSave() {
    String ssid = _server->arg("wifiSsid");
    String host = _server->arg("mainHost");
    String code = _server->arg("pairCode");
    ssid.trim();
    host.trim();
    code.trim();
    if (!ssid.length() || !host.length()) {
      handleForm("Please fill in the WiFi name and the main unit address.");
      return;
    }
    if (code.length()) {
      if (zx::normalizeCode(code).length() != 12) {
        handleForm("The pairing code has 12 letters/numbers, like K7QM-2XPA-9RTD.");
        return;
      }
    } else if (!_state->paired()) {
      handleForm("Enter the pairing code from Admin > Vendos > Add Vendo.");
      return;
    }
    // A blank password keeps the saved one only for the same network.
    String pass = _server->arg("wifiPassword");
    if (pass.length() || ssid != _state->wifiSsid) _state->wifiPass = pass;
    _state->wifiSsid = ssid;
    _state->mainHost = host;
    if (code.length()) {
      _state->pairCode = code;
      _state->vendoId = 0;          // pair again with the new code
    }
    if (!_state->save()) {
      handleForm("Could not save - please try again.");
      return;
    }
    _server->send(200, "text/html",
                  "<!DOCTYPE html><html><body style='font-family:sans-serif;background:#0f0f23;color:#e8e8ec;"
                  "text-align:center;padding-top:80px;'><h1 style='color:#2dd4c9;'>Saved!</h1>"
                  "<p>Restarting and connecting to the main unit...</p></body></html>");
    delay(500);
    ESP.restart();
  }
};

#endif // SUB_SETUP_H
