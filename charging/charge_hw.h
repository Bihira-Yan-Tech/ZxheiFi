/*
 * charge_hw.h - the Charging Station's hardware: PCF8574 expander (4 port
 * relays + 4 buttons), SSD1306 OLED, buzzer.
 * ZXHEIFI Charging Station firmware (v2)
 *
 * I2C on D2 (SDA) / D1 (SCL):
 *   PCF8574 @ 0x20  P0-P3 = port 1-4 relays, P4-P7 = port 1-4 buttons (to GND)
 *   SSD1306 @ 0x3C  128x64 screen (optional - everything works without it)
 * A PCF8574 pin written 1 is a weak pull-up (so it doubles as an input),
 * written 0 sinks current - which is why active-LOW relay modules (the
 * common kind) are the default.
 */
#ifndef CHARGE_HW_H
#define CHARGE_HW_H

#include <Arduino.h>
#include <Wire.h>
#include "SSD1306Wire.h"
#include "charge_logic.h"

#define PCF8574_ADDR 0x20
#define OLED_ADDR    0x3C

class ChargeHW {
public:
  bool expanderOk = false;
  bool displayOk = false;

  void begin() {
    Wire.begin(D2, D1);
    expanderOk = present(PCF8574_ADDR);
    displayOk = present(OLED_ADDR) && _display.init();
    if (displayOk) {
      _display.flipScreenVertically();
      _display.setFont(ArialMT_Plain_10);
    }
    _out = 0xFF;   // relays off (active-LOW default), buttons as inputs
    write();
    Serial.printf("Expander %s, screen %s\n", expanderOk ? "OK" : "NOT FOUND (check wiring)",
                  displayOk ? "OK" : "not found");
  }

  // Port relays. activeHigh=false (default): LOW switches a relay on.
  void setPorts(const bool on[4], uint8_t ports, bool activeHigh) {
    uint8_t out = 0xF0;               // P4-P7 stay 1 = button inputs
    for (uint8_t i = 0; i < 4; i++) {
      bool want = i < ports && on[i];
      if (want == activeHigh) out |= (1 << i);
    }
    if (out != _out) {
      _out = out;
      write();
    }
  }

  // Ports (bitmask, bit 0 = port 1) whose button was just pressed.
  // Debounced: a change must hold for 40 ms.
  uint8_t pressedButtons() {
    if (!expanderOk || millis() - _lastRead < 10) return 0;
    _lastRead = millis();
    Wire.requestFrom((uint8_t)PCF8574_ADDR, (uint8_t)1);
    if (!Wire.available()) return 0;
    uint8_t raw = (uint8_t)(~Wire.read() >> 4) & 0x0F;   // pressed = LOW
    if (raw != _rawLast) {
      _rawLast = raw;
      _rawSince = millis();
      return 0;
    }
    if (millis() - _rawSince < 40 || raw == _stable) return 0;
    uint8_t newly = raw & ~_stable;
    _stable = raw;
    return newly;
  }

  SSD1306Wire& display() { return _display; }

private:
  SSD1306Wire _display{OLED_ADDR, D2, D1};
  uint8_t _out = 0xFF;
  uint8_t _rawLast = 0, _stable = 0;
  uint32_t _rawSince = 0, _lastRead = 0;

  static bool present(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
  }

  void write() {
    if (!expanderOk) return;
    Wire.beginTransmission(PCF8574_ADDR);
    Wire.write(_out);
    Wire.endTransmission();
  }
};

#endif // CHARGE_HW_H
