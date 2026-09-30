/*
 * charging.ino - ZxheiFi Charging Station
 * ZXHEIFI Charging Station firmware (v2) - NodeMCU ESP8266
 *
 * A coin-op phone charger with up to 4 USB ports. The customer presses a
 * port's button, drops coins, and that port gets power for the minutes
 * the coins buy (Admin > Settings > Charging). No phone needed - handy,
 * since the phone is usually the thing that's dead.
 *
 * The box runs the charging itself (charge_logic.h), so it keeps working
 * with no WiFi or with the main unit down. Every sale is written to flash
 * first (up to 100 waiting) and sent to the main unit over the signed
 * protocol until it's acknowledged - nothing is lost or counted twice.
 * Prices, pins and port count come from the main unit and are refetched
 * whenever they change; admin Stops arrive in poll replies.
 *
 * Wiring (details: docs/15-charging-station.md):
 *   coin acceptor signal D5, acceptor power relay D7 (or none), buzzer D8,
 *   I2C SDA D2 / SCL D1 -> PCF8574 (P0-P3 port relays, P4-P7 buttons)
 *   and an optional SSD1306 128x64 screen.
 * LED: double blink = not paired, slow = offline (charging still works),
 *      solid = online, fast = a customer is inserting coins,
 *      triple = expander missing or sales can't be sent for a long time.
 */
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <DNSServer.h>   // used by common/box/ (listed here so PlatformIO links it)
#include <LittleFS.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include "SSD1306Wire.h"
#include "zx_protocol.h"
#include "box/box_config.h"
#include "box/box_store.h"
#include "box/record_queue.h"
#include "box/box_setup.h"
#include "box/box_link.h"
#include "charge_logic.h"
#include "charge_hw.h"

#define CHARGING_FIRMWARE_VERSION "2.0.0-dev"
#define CHARGE_SETUP_AP_SSID      "ZxheiFi-Charge-Setup"
#define SALES_FILE                "/sales.json"
#define PORTS_FILE                "/ports.json"
#define SALES_CAPACITY            100
#define PORTS_SAVE_MS             30000

BoxState state;
BoxLink link(state);
RecordQueue sales(SALES_FILE, SALES_CAPACITY);
ESP8266WebServer server(80);
BoxSetup setupWizard(CHARGE_SETUP_AP_SSID, "ZxheiFi Charging Station", CHARGING_FIRMWARE_VERSION);
bool inSetup = false;
zxc::ChargeLogic logic;
ChargeHW hw;

// From the main unit's config (Admin > Vendos > Edit, Settings > Charging)
String coinPin = "D5", relayPin = "D7";
bool relayActiveHigh = true;
uint16_t pesosPerPulse = 1;
uint8_t portCount = 4;
bool portActiveHigh = false;
bool langEn = false;

bool keyRejected = false;
bool saleSendFailed = false;
bool linkOk = false;
uint32_t lastPollOkMs = 0, lastPollTryMs = 0, lastPairTryMs = 0, lastSaleSendMs = 0;
uint32_t lastPortsSaveMs = 0, lastDrawMs = 0;
bool portsDirty = false;
uint32_t overflowPeso = 0;      // sales that didn't fit in the queue - sent as one record later
bool acceptorOn = false;

volatile uint32_t pulseCount = 0, lastPulseMicros = 0, lastPulseMs = 0;
int coinGpio = -1, relayGpio = -1;

void IRAM_ATTR onPulse() {
  uint32_t now = micros();
  if (now - lastPulseMicros < MIN_PULSE_INTERVAL_US) return;   // contact bounce
  lastPulseMicros = now;
  lastPulseMs = millis();
  pulseCount++;
}

const char* T(const char* tl, const char* en) { return langEn ? en : tl; }

void beep(uint8_t times, uint16_t ms = 60) {
  for (uint8_t i = 0; i < times; i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(ms);
    digitalWrite(PIN_BUZZER, LOW);
    if (i + 1 < times) delay(60);
  }
}

void setAcceptor(bool on) {
  acceptorOn = on;
  if (relayGpio >= 0) digitalWrite(relayGpio, (on == relayActiveHigh) ? HIGH : LOW);
}

// ---- config -----------------------------------------------------------

void applyConfig() {
  DynamicJsonDocument c(1536);
  if (deserializeJson(c, state.configJson)) c.clear();
  String coin = c["coinPin"] | coinPin;
  String relay = c["relayPin"] | relayPin;
  if (coin == "D5" || coin == "D6" || coin == "D7") coinPin = coin;
  if (relay == "none" || relay == "D5" || relay == "D6" || relay == "D7") relayPin = relay;
  relayActiveHigh = c["relayActiveHigh"] | relayActiveHigh;
  uint16_t pulse = c["pesosPerPulse"] | pesosPerPulse;
  if (pulse >= 1 && pulse <= 100) pesosPerPulse = pulse;
  uint8_t ports = c["ports"] | portCount;
  if (ports >= 1 && ports <= 4) portCount = ports;
  portActiveHigh = c["portActiveHigh"] | portActiveHigh;
  langEn = String((const char*)(c["lang"] | "tl")) == "en";

  zxc::ChargeRate rates[zxc::ChargeLogic::MAX_RATES];
  uint8_t n = 0;
  for (JsonObject r : c["rates"].as<JsonArray>()) {
    if (n >= zxc::ChargeLogic::MAX_RATES) break;
    rates[n++] = {(uint32_t)(r["peso"] | 0UL), (uint32_t)(r["minutes"] | 0UL)};
  }
  logic.configure(portCount, rates, n, c["maxMinutes"] | 180UL);

  // pins
  if (coinGpio >= 0) detachInterrupt(digitalPinToInterrupt(coinGpio));
  if (relayGpio >= 0) pinMode(relayGpio, INPUT);
  coinGpio = boxPinGpio(coinPin);
  if (coinGpio < 0) coinGpio = boxPinGpio("D5");
  relayGpio = relayPin == "none" ? -1 : boxPinGpio(relayPin);
  if (relayGpio == coinGpio) relayGpio = -1;
  if (relayGpio >= 0) pinMode(relayGpio, OUTPUT);
  setAcceptor(false);
  pinMode(coinGpio, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(coinGpio), onPulse, FALLING);
  Serial.printf("Config: %u ports, %u rates, max %lu min, coin %s, relay %s\n", portCount, n,
                (unsigned long)(c["maxMinutes"] | 180UL), coinPin.c_str(), relayPin.c_str());
}

// ---- persistence of port times + overflow ------------------------------

void savePorts() {
  uint32_t secs[4];
  logic.snapshot(secs);
  StaticJsonDocument<192> d;
  JsonArray arr = d.createNestedArray("secs");
  for (uint8_t i = 0; i < 4; i++) arr.add(secs[i]);
  d["overflow"] = overflowPeso;
  File f = LittleFS.open(String(PORTS_FILE) + ".tmp", "w");
  if (!f) return;
  serializeJson(d, f);
  f.close();
  LittleFS.remove(PORTS_FILE);
  LittleFS.rename(String(PORTS_FILE) + ".tmp", PORTS_FILE);
  lastPortsSaveMs = millis();
  portsDirty = false;
}

void loadPorts() {
  File f = LittleFS.open(PORTS_FILE, "r");
  if (!f) return;
  StaticJsonDocument<192> d;
  bool bad = (bool)deserializeJson(d, f);
  f.close();
  if (bad) return;
  uint32_t secs[4] = {0, 0, 0, 0};
  uint8_t i = 0;
  for (JsonVariant v : d["secs"].as<JsonArray>()) {
    if (i >= 4) break;
    secs[i++] = v | 0UL;
  }
  logic.restore(secs, millis());
  overflowPeso = d["overflow"] | 0UL;
  Serial.printf("Resumed ports: %lu %lu %lu %lu s\n", (unsigned long)secs[0], (unsigned long)secs[1],
                (unsigned long)secs[2], (unsigned long)secs[3]);
}

// ---- sales -----------------------------------------------------------

void recordSale(const zxc::ChargeSale& s) {
  if (!sales.push(s.peso, 0, s.port, s.minutes)) {
    overflowPeso += s.peso;       // never lost: sent as one record once there's room
    Serial.printf("Sales queue full - PHP %u kept as overflow\n", s.peso);
  }
  portsDirty = true;
  saleSendFailed = false;
}

void flushOverflow() {
  if (!overflowPeso || sales.full()) return;
  uint32_t peso = overflowPeso > 1000 ? 1000 : overflowPeso;   // the main unit takes up to 1000 per record
  if (sales.push(peso, 0, 0, 0)) {
    overflowPeso -= peso;
    portsDirty = true;
  }
}

void sendNextSale() {
  lastSaleSendMs = millis();
  const BoxRecord r = sales.front();
  StaticJsonDocument<192> req;
  uint32_t n = link.nextCounter();
  req["v"] = state.vendoId;
  req["n"] = n;
  req["seq"] = r.seq;
  req["peso"] = r.peso;
  req["port"] = r.port;
  req["minutes"] = r.minutes;
  StaticJsonDocument<128> resp;
  int code = link.postSigned("/api/vendo/charge", req, state.key, resp);
  if (code == 401) {
    keyRejected = true;
    saleSendFailed = true;
    return;
  }
  if (code == 400) {   // the main unit will never accept this record - don't let it block the rest
    Serial.printf("Sale #%u (PHP %u) rejected by the main unit - dropped\n", r.seq, r.peso);
    sales.pop();
    saleSendFailed = false;
    return;
  }
  if (code == 200 && (resp["n"] | 0UL) == n && (resp["ok"] | false)) {
    sales.pop();
    lastPollOkMs = millis();
    saleSendFailed = false;
    return;
  }
  saleSendFailed = true;
}

// ---- link ------------------------------------------------------------

void tryPair() {
  lastPairTryMs = millis();
  DynamicJsonDocument resp(1536);
  if (!link.pair(resp)) return;
  sales.raiseSeq(resp["lastCoinSeq"] | 0UL);
  keyRejected = false;
  applyConfig();
  beep(3);
}

void fetchConfig() {
  StaticJsonDocument<64> req;
  uint32_t n = link.nextCounter();
  req["v"] = state.vendoId;
  req["n"] = n;
  DynamicJsonDocument resp(1536);
  if (link.postSigned("/api/vendo/config", req, state.key, resp) != 200 || (resp["n"] | 0UL) != n) return;
  resp.remove("n");
  state.applyConfig(resp.as<JsonVariantConst>());
  state.save();
  applyConfig();
}

void pollMain() {
  lastPollTryMs = millis();
  StaticJsonDocument<256> req;
  uint32_t n = link.nextCounter();
  req["v"] = state.vendoId;
  req["n"] = n;
  uint32_t secs[4];
  logic.snapshot(secs);
  JsonArray ports = req.createNestedArray("ports");
  for (uint8_t i = 0; i < portCount; i++) ports.add(secs[i]);
  req["ackStop"] = state.lastStopId;
  DynamicJsonDocument resp(768);
  int code = link.postSigned("/api/vendo/poll", req, state.key, resp);
  if (code == 401) {
    if (!keyRejected) Serial.println("Main unit no longer knows this box - re-pair it (Admin > Vendos)");
    keyRejected = true;
    return;
  }
  if (code != 200 || (resp["n"] | 0UL) != n) return;
  keyRejected = false;
  lastPollOkMs = millis();
  bool stopped = false;
  for (JsonObject st : resp["stop"].as<JsonArray>()) {
    uint32_t id = st["id"] | 0UL;
    if (id <= state.lastStopId) continue;
    uint8_t port = st["port"] | 0;
    uint32_t left = logic.stop(port);
    Serial.printf("Admin stopped port %u (%lu s left)\n", port, (unsigned long)(left / 1000));
    state.lastStopId = id;
    stopped = true;
  }
  if (stopped) {
    state.save();
    savePorts();
    beep(2);
  }
  if ((uint32_t)(resp["cfgVer"] | 0UL) != state.cfgVer) fetchConfig();
}

// ---- coins + buttons -------------------------------------------------

void processPulses() {
  if (!pulseCount || millis() - lastPulseMs < COIN_DEBOUNCE_MS) return;
  noInterrupts();
  uint32_t pulses = pulseCount;
  pulseCount = 0;
  interrupts();
  if (pulses > MAX_PLAUSIBLE_PULSES) {
    Serial.printf("Ignoring implausible burst of %u pulses - check the wiring/acceptor\n", pulses);
    return;
  }
  uint32_t peso = pulses * pesosPerPulse;
  zxc::ChargeSale sale;
  if (logic.coin(peso, millis(), sale)) {
    recordSale(sale);
    savePorts();
    Serial.printf("PHP %u -> port %u +%lu min\n", peso, sale.port, (unsigned long)sale.minutes);
  } else {
    Serial.printf("PHP %u held - press a port within 60 s\n", peso);
  }
  beep(1);
}

void processButtons() {
  uint8_t pressed = hw.pressedButtons();
  for (uint8_t i = 0; i < 4; i++) {
    if (!(pressed & (1 << i))) continue;
    zxc::ChargeSale claimed;
    bool hasClaim = false;
    logic.press(i + 1, millis(), &claimed, &hasClaim);
    if (hasClaim) {
      recordSale(claimed);
      savePorts();
    }
    beep(1, 30);
  }
}

// ---- screen ----------------------------------------------------------

String hms(uint32_t ms) {
  uint32_t s = (ms + 999) / 1000;
  char buf[12];
  snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60),
           (unsigned long)(s % 60));
  return buf;
}

void drawScreen() {
  if (!hw.displayOk || millis() - lastDrawMs < 250) return;
  lastDrawMs = millis();
  SSD1306Wire& d = hw.display();
  d.clear();
  d.setTextAlignment(TEXT_ALIGN_LEFT);
  d.setFont(ArialMT_Plain_10);
  bool blinkOn = (millis() / 500) % 2;

  if (!state.paired() || keyRejected || !logic.hasRates()) {
    d.drawString(0, 0, "ZxheiFi Charging");
    if (!state.paired() || keyRejected) {
      d.drawString(0, 16, T("Hindi pa naka-setup.", "Not set up yet."));
      d.drawString(0, 28, T("WiFi: ZxheiFi-Charge-Setup", "WiFi: ZxheiFi-Charge-Setup"));
      d.drawString(0, 40, T("(FLASH pagka-on)", "(FLASH after power-on)"));
    } else {
      d.drawString(0, 16, T("Walang presyo pa.", "No prices yet."));
      d.drawString(0, 28, T("Admin > Settings", "Admin > Settings"));
    }
    // ports still running keep showing their time
    for (uint8_t p = 1; p <= portCount; p++) {
      if (logic.portOn(p)) d.drawString(0, 52, String(p) + ": " + hms(logic.remainingMs(p)));
    }
    d.display();
    return;
  }

  uint8_t sel = logic.selected();
  if (sel) {
    d.setFont(ArialMT_Plain_16);
    d.drawString(0, 0, "Port " + String(sel));
    d.setFont(ArialMT_Plain_10);
    d.drawString(70, 4, String(logic.windowLeftSec(millis())) + "s");
    if (logic.atCap(sel)) {
      d.drawString(0, 20, T("Max na ang oras", "Time limit reached"));
    } else {
      d.drawString(0, 20, T("Maghulog ng barya", "Insert coins"));
    }
    d.drawString(0, 34, "P" + String(logic.windowPeso()) + " = " + String(logic.windowMinutes()) + " min");
    d.drawString(0, 48, T("Oras: ", "Time: ") + hms(logic.remainingMs(sel)));
  } else {
    d.drawString(0, 0, "ZxheiFi Charging");
    if (!linkOk) d.drawString(120, 0, "!");
    for (uint8_t p = 1; p <= portCount; p++) {
      int x = (p - 1) % 2 ? 64 : 0;
      int y = 14 + ((p - 1) / 2) * 14;
      String txt = String(p) + ": ";
      if (!logic.portOn(p)) txt += T("LIBRE", "FREE");
      else if (!logic.warn(p) || blinkOn) txt += hms(logic.remainingMs(p));
      d.drawString(x, y, txt);
    }
    if (logic.heldPeso()) d.drawString(0, 50, "P" + String(logic.heldPeso()) + T(" - pumili ng port", " - choose a port"));
    else d.drawString(0, 50, T("Pindutin ang port", "Press a port button"));
  }
  d.display();
}

void updateLed(bool wifiUp) {
  uint32_t t = millis();
  bool on;
  if (!state.paired() || keyRejected) {
    uint32_t p = t % 1500;
    on = p < 100 || (p >= 250 && p < 350);                              // double blink
  } else if (!hw.expanderOk || (sales.full() && overflowPeso)) {
    uint32_t p = t % 1500;
    on = p < 100 || (p >= 250 && p < 350) || (p >= 500 && p < 600);     // triple blink
  } else if (!wifiUp || !linkOk) {
    on = (t / 500) % 2;                                                 // slow blink
  } else if (acceptorOn) {
    on = (t / 100) % 2;                                                 // fast blink
  } else {
    on = true;
  }
  digitalWrite(PIN_LED_STATUS, on ? LOW : HIGH);
}

// ---- main ------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.printf("\nZxheiFi Charging Station %s\n", CHARGING_FIRMWARE_VERSION);
  pinMode(PIN_LED_STATUS, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  if (!LittleFS.begin()) {
    Serial.println("Formatting flash storage...");
    LittleFS.format();
    LittleFS.begin();
  }
  state.load();
  sales.load();
  link.begin();
  hw.begin();
  applyConfig();
  loadPorts();                  // resume charging after a power cut

  if (!state.configured() || BoxSetup::buttonPressedInWindow()) {
    inSetup = true;
    setupWizard.begin(server, state);
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (state.wifiPass.length()) WiFi.begin(state.wifiSsid.c_str(), state.wifiPass.c_str());
  else WiFi.begin(state.wifiSsid.c_str());
  Serial.printf("WiFi %s, main unit %s, %s, %u sale(s) waiting\n", state.wifiSsid.c_str(), state.mainHost.c_str(),
                state.paired() ? ("vendo " + String(state.vendoId)).c_str() : "not paired yet",
                (unsigned)sales.size());
}

void loop() {
  if (inSetup) {
    setupWizard.loop();
    return;
  }
  uint32_t now = millis();

  // the charging itself - works with or without the main unit
  zxc::ChargeSale expired;
  bool hasExpired = false;
  logic.tick(now, &expired, &hasExpired);
  if (hasExpired) recordSale(expired);   // a held coin nobody claimed: still a sale
  logic.setEnabled(state.paired() && !keyRejected && hw.expanderOk);
  processButtons();
  processPulses();
  flushOverflow();

  bool on[4];
  for (uint8_t i = 0; i < 4; i++) on[i] = logic.portOn(i + 1);
  hw.setPorts(on, portCount, portActiveHigh);
  bool wantAcceptor = logic.acceptorOn();
  if (wantAcceptor != acceptorOn) setAcceptor(wantAcceptor);

  if (portsDirty || millis() - lastPortsSaveMs >= PORTS_SAVE_MS) savePorts();

  // the link to the main unit
  bool wifiUp = WiFi.status() == WL_CONNECTED;
  if (wifiUp) {
    if (!state.paired()) {
      if (state.pairCode.length() && millis() - lastPairTryMs >= BOX_PAIR_RETRY_MS) tryPair();
    } else {
      uint32_t every = keyRejected ? BOX_REJECTED_POLL_MS : ZX_POLL_IDLE_MS;
      if (millis() - lastPollTryMs >= every) pollMain();
      if (!sales.empty() && !keyRejected && (!saleSendFailed || millis() - lastSaleSendMs >= ZX_COIN_RESEND_MS)) {
        sendNextSale();
      }
    }
  }
  linkOk = state.paired() && !keyRejected && lastPollOkMs && millis() - lastPollOkMs < ZX_VENDO_OFFLINE_MS;
  drawScreen();
  updateLed(wifiUp);
}
