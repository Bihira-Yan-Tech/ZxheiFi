/*
 * subvendo.ino - ZxheiFi Sub Vendo
 * ZXHEIFI Sub Vendo firmware (v2) - NodeMCU ESP8266
 *
 * A second (third, ...) coin box that sells time for the same ZxheiFi
 * hotspot. It only counts coins and switches its coin acceptor's relay;
 * the main unit decides everything else (prices, sessions, sales). They
 * talk over the signed protocol in common/zx_protocol.h:
 *
 *   - pair once with the code from Admin > Vendos > Add Vendo
 *   - poll the main unit every 2 s (1 s while a customer is inserting);
 *     the reply says whether a customer has reserved this box
 *   - relay ON only while reserved AND the main unit answered within the
 *     last 3 s (fail-closed: no link = the acceptor rejects coins)
 *   - every coin is queued in flash first, then sent until the main unit
 *     signs an "ok" - WiFi hiccups can't lose a customer's money
 *
 * LED: double blink = not paired / removed, slow blink = connecting,
 *      solid = online, fast blink = customer inserting, triple = queue
 *      nearly full (check the WiFi). Buzzer: 1 beep per coin, a chirp
 *      when a customer's window opens, 3 beeps when paired.
 * Setup + link + storage: common/box/. Wiring: same pins as the main unit
 * (coin D5, relay D7, buzzer D8) - changeable from Admin > Vendos.
 */
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <DNSServer.h>   // used by common/box/ (listed here so PlatformIO links it)
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "zx_protocol.h"
#include "box/box_config.h"
#include "box/box_store.h"
#include "box/record_queue.h"
#include "box/box_setup.h"
#include "box/box_link.h"
#include "sub_config.h"

BoxState state;
BoxLink link(state);
RecordQueue coinQueue(SUB_QUEUE_FILE, SUB_QUEUE_CAPACITY);
ESP8266WebServer server(80);
BoxSetup setupWizard(SUB_SETUP_AP_SSID, "ZxheiFi Sub Vendo", SUB_FIRMWARE_VERSION);
bool inSetup = false;

// Pins + pulse value, from the main unit's config (Admin > Vendos > Edit)
String coinPin = "D5", relayPin = "D7";
bool relayActiveHigh = true;
uint16_t pesosPerPulse = 1;

bool relayOn = false;
bool pollRelay = false;         // main unit says a customer reserved this box
bool pollFast = false;
volatile uint32_t currentRid = 0;
bool keyRejected = false;       // main unit answered 401 (removed / re-paired)
bool coinSendFailed = false;
uint32_t lastPollOkMs = 0, lastPollTryMs = 0, lastPairTryMs = 0, lastCoinSendMs = 0;

volatile uint32_t pulseCount = 0, lastPulseMicros = 0, lastPulseMs = 0, burstRid = 0;
int coinGpio = -1, relayGpio = -1;

void IRAM_ATTR onPulse() {
  uint32_t now = micros();
  if (now - lastPulseMicros < MIN_PULSE_INTERVAL_US) return;   // contact bounce
  lastPulseMicros = now;
  lastPulseMs = millis();
  if (pulseCount == 0) burstRid = currentRid;   // whose reservation this coin belongs to
  pulseCount++;
}

void beep(uint8_t times) {
  for (uint8_t i = 0; i < times; i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(60);
    digitalWrite(PIN_BUZZER, LOW);
    if (i + 1 < times) delay(60);
  }
}

void setRelay(bool on) {
  relayOn = on;
  if (relayGpio >= 0) digitalWrite(relayGpio, (on == relayActiveHigh) ? HIGH : LOW);
}

// Reads this box's pins from the main unit's last config.
void readConfig() {
  DynamicJsonDocument c(1024);
  if (deserializeJson(c, state.configJson)) return;
  String coin = c["coinPin"] | coinPin;
  String relay = c["relayPin"] | relayPin;
  if (boxPinGpio(coin) >= 0) coinPin = coin;
  if (relay == "none" || boxPinGpio(relay) >= 0) relayPin = relay;
  relayActiveHigh = c["relayActiveHigh"] | relayActiveHigh;
  uint16_t pulse = c["pesosPerPulse"] | pesosPerPulse;
  if (pulse >= 1 && pulse <= 100) pesosPerPulse = pulse;
}

void applyPins() {
  readConfig();
  if (coinGpio >= 0) detachInterrupt(digitalPinToInterrupt(coinGpio));
  if (relayGpio >= 0) pinMode(relayGpio, INPUT);
  coinGpio = boxPinGpio(coinPin);
  if (coinGpio < 0) coinGpio = boxPinGpio("D5");
  relayGpio = relayPin == "none" ? -1 : boxPinGpio(relayPin);
  if (relayGpio == coinGpio) relayGpio = -1;
  if (relayGpio >= 0) pinMode(relayGpio, OUTPUT);
  setRelay(false);
  pinMode(coinGpio, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(coinGpio), onPulse, FALLING);
  Serial.printf("Pins: coin %s, relay %s (%s), PHP %u per pulse\n", coinPin.c_str(), relayPin.c_str(),
                relayActiveHigh ? "HIGH" : "LOW", pesosPerPulse);
}

void tryPair() {
  lastPairTryMs = millis();
  DynamicJsonDocument resp(768);
  if (!link.pair(resp)) return;
  coinQueue.raiseSeq(resp["lastCoinSeq"] | 0UL);
  keyRejected = false;
  applyPins();
  beep(3);
}

void fetchConfig() {
  StaticJsonDocument<64> req;
  uint32_t n = link.nextCounter();
  req["v"] = state.vendoId;
  req["n"] = n;
  DynamicJsonDocument resp(768);
  if (link.postSigned("/api/vendo/config", req, state.key, resp) != 200 || (resp["n"] | 0UL) != n) return;
  resp.remove("n");
  state.applyConfig(resp.as<JsonVariantConst>());
  state.save();
  applyPins();
}

void pollMain() {
  lastPollTryMs = millis();
  StaticJsonDocument<64> req;
  uint32_t n = link.nextCounter();
  req["v"] = state.vendoId;
  req["n"] = n;
  DynamicJsonDocument resp(384);
  int code = link.postSigned("/api/vendo/poll", req, state.key, resp);
  if (code == 401) {
    if (!keyRejected) Serial.println("Main unit no longer knows this box - re-pair it (Admin > Vendos)");
    keyRejected = true;
    return;
  }
  if (code != 200 || (resp["n"] | 0UL) != n) return;
  keyRejected = false;
  lastPollOkMs = millis();
  bool wasReserved = pollRelay;
  pollRelay = resp["relay"] | false;
  currentRid = pollRelay ? (uint32_t)(resp["rid"] | 0UL) : 0;
  pollFast = resp["fast"] | false;
  if ((uint32_t)(resp["cfgVer"] | 0UL) != state.cfgVer) fetchConfig();
  if (pollRelay && !wasReserved) beep(1);   // "insert your coins"
}

void sendNextCoin() {
  lastCoinSendMs = millis();
  const BoxRecord c = coinQueue.front();
  StaticJsonDocument<160> req;
  uint32_t n = link.nextCounter();
  req["v"] = state.vendoId;
  req["n"] = n;
  req["seq"] = c.seq;
  req["peso"] = c.peso;
  req["rid"] = c.rid;
  StaticJsonDocument<128> resp;
  int code = link.postSigned("/api/vendo/coin", req, state.key, resp);
  if (code == 401) {
    keyRejected = true;
    coinSendFailed = true;
    return;
  }
  if (code == 400) {   // the main unit will never accept this record - don't let it block the rest
    Serial.printf("Coin #%u (PHP %u) rejected by the main unit - dropped\n", c.seq, c.peso);
    coinQueue.pop();
    coinSendFailed = false;
    return;
  }
  if (code == 200 && (resp["n"] | 0UL) == n && (resp["ok"] | false)) {
    coinQueue.pop();
    lastPollOkMs = millis();
    coinSendFailed = false;
    Serial.printf("Coin #%u (PHP %u) acknowledged%s\n", c.seq, c.peso, (resp["dup"] | false) ? " (already had it)" : "");
    return;
  }
  coinSendFailed = true;
}

void processPulses() {
  if (!pulseCount || millis() - lastPulseMs < COIN_DEBOUNCE_MS) return;
  noInterrupts();
  uint32_t pulses = pulseCount;
  uint32_t rid = burstRid;
  pulseCount = 0;
  interrupts();
  if (pulses > MAX_PLAUSIBLE_PULSES) {
    Serial.printf("Ignoring implausible burst of %u pulses - check the wiring/acceptor\n", pulses);
    return;
  }
  uint32_t peso = pulses * pesosPerPulse;
  if (!coinQueue.push(peso, rid)) {
    Serial.printf("COIN QUEUE FULL - PHP %u NOT RECORDED (main unit unreachable?)\n", peso);
    beep(4);
    return;
  }
  Serial.printf("Coin PHP %u queued (rid %u)\n", peso, rid);
  beep(1);
  coinSendFailed = false;   // send right away
}

void updateLed(bool wifiUp, bool linkOk) {
  uint32_t t = millis();
  bool on;
  if (!state.paired() || keyRejected) {
    uint32_t p = t % 1500;
    on = p < 100 || (p >= 250 && p < 350);                              // double blink
  } else if (coinQueue.size() + SUB_QUEUE_HEADROOM >= coinQueue.capacity()) {
    uint32_t p = t % 1500;
    on = p < 100 || (p >= 250 && p < 350) || (p >= 500 && p < 600);     // triple blink
  } else if (!wifiUp || !linkOk) {
    on = (t / 500) % 2;                                                 // slow blink
  } else if (relayOn) {
    on = (t / 100) % 2;                                                 // fast blink
  } else {
    on = true;                                                          // solid
  }
  digitalWrite(PIN_LED_STATUS, on ? LOW : HIGH);   // built-in LED is active-low
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.printf("\nZxheiFi Sub Vendo %s\n", SUB_FIRMWARE_VERSION);
  pinMode(PIN_LED_STATUS, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  if (!LittleFS.begin()) {
    Serial.println("Formatting flash storage...");
    LittleFS.format();
    LittleFS.begin();
  }
  state.load();
  coinQueue.load();
  link.begin();

  if (!state.configured() || BoxSetup::buttonPressedInWindow()) {
    inSetup = true;
    setupWizard.begin(server, state);
    return;
  }
  applyPins();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (state.wifiPass.length()) WiFi.begin(state.wifiSsid.c_str(), state.wifiPass.c_str());
  else WiFi.begin(state.wifiSsid.c_str());
  Serial.printf("WiFi %s, main unit %s, %s, %u coin(s) waiting\n", state.wifiSsid.c_str(), state.mainHost.c_str(),
                state.paired() ? ("vendo " + String(state.vendoId)).c_str() : "not paired yet",
                (unsigned)coinQueue.size());
}

void loop() {
  if (inSetup) {
    setupWizard.loop();
    return;
  }
  processPulses();

  bool wifiUp = WiFi.status() == WL_CONNECTED;
  if (wifiUp) {
    if (!state.paired()) {
      if (state.pairCode.length() && millis() - lastPairTryMs >= BOX_PAIR_RETRY_MS) tryPair();
    } else {
      uint32_t every = keyRejected ? BOX_REJECTED_POLL_MS : (pollFast ? ZX_POLL_ACTIVE_MS : ZX_POLL_IDLE_MS);
      if (millis() - lastPollTryMs >= every) pollMain();
      if (!coinQueue.empty() && !keyRejected &&
          (!coinSendFailed || millis() - lastCoinSendMs >= ZX_COIN_RESEND_MS)) {
        sendNextCoin();
      }
    }
  }

  // Fail-closed: the acceptor only takes coins while a customer holds
  // this box AND the main unit answered within the last 3 seconds.
  bool linkOk = state.paired() && !keyRejected && lastPollOkMs && millis() - lastPollOkMs < ZX_SUB_FAILCLOSED_MS;
  bool want = linkOk && pollRelay && coinQueue.size() + SUB_QUEUE_HEADROOM < coinQueue.capacity();
  if (want != relayOn) setRelay(want);
  if (!linkOk && pollRelay && lastPollOkMs && millis() - lastPollOkMs >= ZX_SUB_FAILCLOSED_MS) {
    pollRelay = false;          // stale reservation - wait for a fresh poll
    currentRid = 0;
  }
  updateLed(wifiUp, linkOk);
}
