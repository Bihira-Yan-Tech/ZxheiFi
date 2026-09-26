/*
 * sub_config.h - Sub Vendo constants
 * ZXHEIFI Sub Vendo firmware (v2)
 */
#ifndef SUB_CONFIG_H
#define SUB_CONFIG_H

#include <Arduino.h>

#define SUB_FIRMWARE_VERSION   "2.0.0-dev"
#define SUB_SETUP_AP_SSID      "ZxheiFi-Sub-Setup"
#define SUB_DEFAULT_MAIN_HOST  "nodemcu.zxheifi.lan"   // the main unit (static DNS on the MikroTik)
#define SUB_STATE_FILE         "/sub.json"
#define SUB_QUEUE_FILE         "/queue.json"

// Board pins (same as the main unit's defaults)
#define PIN_LED_STATUS    D4   // built-in LED, lit when LOW
#define PIN_BUZZER        D8
#define PIN_SETUP_BUTTON  D3   // the FLASH button - press during the fast-blink window after boot

#define SETUP_BUTTON_WINDOW_MS 3000

// Coin pulse counting - identical to the main unit (firmware/config.h)
#define COIN_DEBOUNCE_MS       300    // a coin's pulse train is finished after this much silence
#define MIN_PULSE_INTERVAL_US  5000   // closer pulses are contact bounce
#define MAX_PLAUSIBLE_PULSES   50     // bigger bursts are noise/tampering, not a coin

#define SUB_HTTP_TIMEOUT_MS    1500
#define SUB_PAIR_RETRY_MS      15000  // < 5 tries/min, under the main unit's pairing rate limit
#define SUB_REJECTED_POLL_MS   15000  // main said "unpaired": ask again slowly
#define SUB_COUNTER_BLOCK      1000   // message counters reserved per flash write
#define SUB_QUEUE_HEADROOM     2      // relay off this many coins before the queue is full

// NodeMCU pin label -> GPIO for the pins a coin acceptor/relay may use.
// D0 (no interrupt) and D3/D4/D8 (boot pins) are deliberately absent.
static int subPinGpio(const String& label) {
  if (label == "D1") return 5;
  if (label == "D2") return 4;
  if (label == "D5") return 14;
  if (label == "D6") return 12;
  if (label == "D7") return 13;
  return -1;
}

#endif // SUB_CONFIG_H
