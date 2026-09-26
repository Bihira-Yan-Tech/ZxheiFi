/*
 * nodemcu_firmware.ino - Main firmware entry point
 * ZXHEIFI - NodeMCU ESP8266 Firmware
 * Size constraint: <900KB (see config.h's SIZE CONSTRAINTS section for why)
 *
 * Talks to a MikroTik router (RouterOS API, see mikrotik_api.h) to
 * provision/revoke Hotspot users and PPPoE secrets as vouchers are
 * redeemed, tracks time+data budgets locally (session.h), and exposes a
 * small JSON API (gui_handler.h) for the MikroTik-hosted GUI's
 * script.js. See docs/06-gui-customize.md for the GUI deployment step.
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <MD5Builder.h>
#include <time.h>
#include "config.h"

// All modules are header-only (class defined + implemented in the .h) so
// the Arduino build system doesn't try to compile them a second time as
// standalone .cpp translation units.
#include "mikrotik_api.h"
#include "session.h"
#include "qos.h"
#include "pppoe.h"
#include "admin_api.h"
#include "telegram.h"
#include "vendo_registry.h"
#include "coin_slot.h"
#include "gui_handler.h"
#include "network_config.h"
#include "setup_mode.h"

ESP8266WebServer server(80);
MikrotikAPI mikrotikApi;
SessionManager sessionManager;
PPPoEManager pppoeManager;
QoSManager qosManager;
AdminAPI adminAPI;
TelegramNotifier telegramNotifier;
VendoRegistry vendoRegistry;
CoinSlot coinSlot;
GUIHandler guiHandler;
NetworkConfig networkConfig;
SetupModeManager setupModeManager;
bool inSetupMode = false;
bool hasNetworkConfig = false; // true once networkConfig.load() succeeds

// System state
bool isMikrotikReachable = false;
bool mikrotikStateKnown = false; // suppresses a spurious "restored" log on the very first check
uint32_t lastSessionSave = 0;
uint32_t lastMikrotikCheck = 0;
uint32_t lastDataQuery = 0;
uint32_t lastSecondTick = 0;
uint32_t lastDailyCounterCheck = 0;

volatile uint32_t coinPulseCount = 0;
volatile uint32_t lastPulseMicros = 0;
volatile uint32_t lastPulseMs = 0;

void IRAM_ATTR onCoinPulse() {
  // Reject pulses arriving faster than MIN_PULSE_INTERVAL_US - almost
  // always contact bounce/electrical noise on the acceptor's line, not
  // a second coin (see docs/11-coin-acceptor-wiring.md).
  uint32_t now = micros();
  if (now - lastPulseMicros < MIN_PULSE_INTERVAL_US) return;
  lastPulseMicros = now;
  lastPulseMs = millis(); // drives processCoinSlot()'s debounce below
  coinPulseCount++;
}

// Settings > Coin Slot pins, applied at boot and whenever Settings is
// saved (GUIHandler calls this back) - no reflash needed for a baseboard
// that wires the acceptor to a different pin.
int coinGpio = -1;
void applyCoinPins() {
  if (coinGpio >= 0) detachInterrupt(digitalPinToInterrupt(coinGpio));
  coinGpio = coinPinGpio(adminAPI.coinPin);
  if (coinGpio < 0) coinGpio = coinPinGpio("D5");
  pinMode(coinGpio, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(coinGpio), onCoinPulse, FALLING);
  coinSlot.applyRelayPin();
  Serial.printf("Coin slot: coin pin %s, relay pin %s (%s trigger), PHP %u per pulse\n",
                adminAPI.coinPin.c_str(), adminAPI.relayPin.c_str(),
                adminAPI.relayActiveHigh ? "HIGH" : "LOW", adminAPI.coinPulseValue);
}

// The admin LED (D6) is skipped if the coin slot was moved onto that pin.
bool adminLedFree() {
  return adminAPI.coinPin != "D6" && adminAPI.relayPin != "D6";
}

void loadConfig() {
  // AdminAPI.begin() reads config.json (settings) and vouchers.json (the
  // redeemable pool) from SPIFFS; both are safe to call on first boot
  // when neither file exists yet. If the Setup Wizard saved a chosen
  // admin password, it seeds the first super admin instead of the
  // ADMIN_PASSWORD_HASH default - ignored on every boot after the first.
  adminAPI.begin(hasNetworkConfig ? networkConfig.initialAdminPassword : "");
  telegramNotifier.enabled = adminAPI.telegramEnabled;
  telegramNotifier.botToken = adminAPI.telegramBotToken;
  telegramNotifier.chatId = adminAPI.telegramChatId;
}

// Joins the customer WiFi the way it is ACTUALLY set up right now. With a
// saved password the ESP8266 refuses an open network - so switching the
// hAP lite to an open (piso-WiFi style) SSID left the NodeMCU offline
// until its Setup Wizard was re-run. Scanning first also keeps a
// password network working when the router is still booting (e.g. after
// a brownout): if the SSID isn't visible yet, the saved password is used
// and wifiWatchdog() below re-checks every 30s.
bool wifiUsingOpen = false;
void beginWiFi(const char* ssid, const char* password) {
  bool open = false;
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == ssid) {
      open = WiFi.encryptionType(i) == ENC_TYPE_NONE;
      break;
    }
  }
  WiFi.scanDelete();
  wifiUsingOpen = open;
  if (open || !password || !password[0]) {
    WiFi.begin(ssid);
    if (password && password[0]) Serial.printf("WiFi %s is open - connecting without the saved password\n", ssid);
  } else {
    WiFi.begin(ssid, password);
  }
}

uint32_t lastWifiCheck = 0;
void wifiWatchdog() {
  if (WiFi.status() == WL_CONNECTED || millis() - lastWifiCheck < 30000) return;
  lastWifiCheck = millis();
  const char* ssid = hasNetworkConfig ? networkConfig.wifiSsid.c_str() : WIFI_SSID;
  const char* password = hasNetworkConfig ? networkConfig.wifiPassword.c_str() : WIFI_PASSWORD;
  Serial.println("WiFi not connected - checking the network again");
  beginWiFi(ssid, password);
}

void connectToMikrotik() {
  // The NodeMCU joins the MikroTik AP as a WiFi station (see
  // mikrotik/hAP_lite_full_config.rsc section 2 for the SSID/PSK this
  // must match). It then talks to the router over that link for both
  // the RouterOS API (port 8728) and, once online, NTP. Prefers the
  // Setup Wizard's saved WiFi credentials (network.json) over the
  // compile-time WIFI_SSID/WIFI_PASSWORD constants when available.
  const char* ssid = hasNetworkConfig ? networkConfig.wifiSsid.c_str() : WIFI_SSID;
  const char* password = hasNetworkConfig ? networkConfig.wifiPassword.c_str() : WIFI_PASSWORD;

  WiFi.mode(WIFI_STA);
  beginWiFi(ssid, password);

  Serial.printf("Connecting to %s", ssid);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi connected, IP: %s\n", WiFi.localIP().toString().c_str());
    // NODEMCU_MDNS_NAME.local is for the PC/admin side. Customer phones
    // reach this device as nodemcu.zxheifi.lan instead - a static DNS
    // entry plus a DHCP reservation the desktop app puts on the router -
    // because captive-portal browsers on iOS/Android don't resolve .local.
    if (MDNS.begin(NODEMCU_MDNS_NAME)) {
      MDNS.addService("http", "tcp", 80);
      Serial.printf("mDNS responder started: %s.local\n", NODEMCU_MDNS_NAME);
    } else {
      Serial.println("mDNS responder failed to start");
    }
  } else {
    Serial.println("WiFi connect timed out - will keep retrying in the background");
  }
}

void checkMikrotikConnection() {
  bool wasReachable = isMikrotikReachable;
  isMikrotikReachable = mikrotikApi.ping();
  guiHandler.setMikrotikReachable(isMikrotikReachable);
  if (adminLedFree()) digitalWrite(PIN_LED_ADMIN, isMikrotikReachable ? HIGH : LOW);
  if (!isMikrotikReachable) {
    Serial.println("MikroTik API unreachable");
  }
  // Log only the transition, not every 30s poll - avoids flooding the
  // rolling 300-entry log during an extended outage. Skipped on the
  // very first check after boot (nothing was "lost" or "restored" yet).
  if (mikrotikStateKnown) {
    if (wasReachable && !isMikrotikReachable) {
      adminAPI.logEvent("mikrotik_lost", "");
    } else if (!wasReachable && isMikrotikReachable) {
      adminAPI.logEvent("mikrotik_restored", "");
    }
  }
  mikrotikStateKnown = true;
}

// Pulls a fresh bytes-in+out reading for every active session and lets
// SessionManager deduct the delta (Time+Data combo tracking). Paused
// sessions have nothing to poll - they're deprovisioned on MikroTik.
void queryTrafficStats() {
  for (uint8_t i = 0; i < sessionManager.count(); i++) {
    SessionEntry& e = sessionManager.entryAt(i);
    if (!e.active || e.paused) continue;
    uint64_t totalBytes;
    if (pppoeManager.pollTraffic(mikrotikApi, e.sessionId, e.mode, totalBytes)) {
      uint64_t used = sessionManager.applyTrafficSample(e.sessionId, totalBytes);
      if (used) {
        adminAPI.dataUsedTodayBytes += used;
        adminAPI.todayDirty = true;
      }
    }
  }
}

// Reconciles the local session table against what's actually still
// active on MikroTik: expired/kicked sessions get deprovisioned, the
// per-second time budget is deducted here (Idle Auto-Pause and the
// Pause button both skip this inside SessionManager::tickSeconds), and
// sessions paused past MAX_PAUSE_MINUTES are dropped outright.
void queryActiveSessions(uint32_t elapsedSeconds) {
  for (uint8_t i = 0; i < sessionManager.count(); i++) {
    SessionEntry& e = sessionManager.entryAt(i);
    if (!e.active) continue;

    if (e.paused) {
      if (sessionManager.isPausedTooLong(e.sessionId)) {
        sessionManager.removeSession(e.sessionId); // already deprovisioned when paused
        Serial.printf("Session %s paused too long, dropped\n", e.sessionId.c_str());
      }
      continue;
    }

    sessionManager.tickSeconds(e.sessionId, elapsedSeconds);

    if (sessionManager.isOver(e.sessionId)) {
      pppoeManager.deprovision(mikrotikApi, e.sessionId, e.mode);
      sessionManager.removeSession(e.sessionId);
      Serial.printf("Session %s expired, revoked on MikroTik\n", e.sessionId.c_str());
    }
  }
}

// Subscriptions don't run down a time/data budget - they're cut off
// purely by expiry date. Checked once a minute alongside the daily
// counter rollover.
void checkSubscriptions() {
  for (uint8_t i = 0; i < sessionManager.count(); i++) {
    SessionEntry& e = sessionManager.entryAt(i);
    if (!e.active || !e.isSubscriber || e.paused) continue;

    Subscriber* sub = adminAPI.findSubscriber(e.sessionId);
    if (!sub || !sub->active || adminAPI.isSubscriberExpired(*sub)) {
      pppoeManager.deprovision(mikrotikApi, e.sessionId, e.mode);
      sessionManager.removeSession(e.sessionId);
      Serial.printf("Subscription %s expired or deactivated, revoked on MikroTik\n", e.sessionId.c_str());
    }
  }
}

// Physical coin-slot handling: each debounced burst of pulses is one
// coin drop worth pulses x COIN_PULSE_VALUE_PHP. Who it belongs to, and
// how it becomes internet time, is coin_slot.h's job.
void processCoinSlot() {
  if (coinPulseCount == 0) return;
  // lastPulseMs is updated by the ISR on every accepted pulse, so this
  // keeps waiting as long as the burst is still arriving.
  if (millis() - lastPulseMs < COIN_DEBOUNCE_MS) return;

  noInterrupts();
  uint32_t pulses = coinPulseCount;
  coinPulseCount = 0;
  interrupts();

  if (pulses > MAX_PLAUSIBLE_PULSES) {
    Serial.printf("Coin slot: ignoring implausible burst of %u pulses (max %u) - check wiring/acceptor\n",
                  pulses, MAX_PLAUSIBLE_PULSES);
    adminAPI.logEvent("coin_anomaly", String(pulses) + " pulses ignored (implausible burst)");
    return; // discard - don't credit anything for a burst this size
  }

  uint32_t peso = pulses * adminAPI.coinPulseValue;
  Serial.printf("Coin slot: PHP %u coin\n", peso);
  digitalWrite(PIN_BUZZER, HIGH);
  delay(50);
  digitalWrite(PIN_BUZZER, LOW);
  vendoRegistry.addToBox(0, peso);   // the cash is in the box whatever happens to the credit
  coinSlot.addCoin(0, peso);
}

void updateStatusLED() {
  // Solid = online & MikroTik reachable, slow blink = degraded. The
  // built-in LED on D4 lights when the pin is LOW (it was inverted).
  static uint32_t lastBlink = 0;
  if (isMikrotikReachable) {
    digitalWrite(PIN_LED_STATUS, LOW);
  } else if (millis() - lastBlink > 500) {
    digitalWrite(PIN_LED_STATUS, !digitalRead(PIN_LED_STATUS));
    lastBlink = millis();
  }
}

void setupRoutes() {
  vendoRegistry.begin();
  coinSlot.begin(sessionManager, adminAPI, pppoeManager, qosManager, mikrotikApi, telegramNotifier, vendoRegistry);
  applyCoinPins();
  guiHandler.begin(server, sessionManager, adminAPI, pppoeManager, qosManager, mikrotikApi, telegramNotifier);
  guiHandler.setCoinSlot(coinSlot);
  guiHandler.setVendoRegistry(vendoRegistry);
  guiHandler.onCoinPinsChanged = applyCoinPins;
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ZXHEIFI NODEMCU FIRMWARE ===");

  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS Mount Failed - Formatting...");
    SPIFFS.format();
    SPIFFS.begin();
  }

  // Setup Mode runs INSTEAD of everything below, never alongside it -
  // see setup_mode.h. Triggered by a genuinely fresh device (no
  // network.json saved yet) or by pressing the onboard FLASH button
  // while the LED blinks fast right after boot.
  if (SetupModeManager::shouldEnter()) {
    inSetupMode = true;
    setupModeManager.begin(server);
    return;
  }

  hasNetworkConfig = networkConfig.load();
  if (hasNetworkConfig) {
    mikrotikApi.host = networkConfig.mikrotikHost;
    mikrotikApi.apiUser = networkConfig.mikrotikApiUser;
    mikrotikApi.apiPass = networkConfig.mikrotikApiPass;
  }

  loadConfig();

  pinMode(PIN_LED_STATUS, OUTPUT);
  if (adminLedFree()) pinMode(PIN_LED_ADMIN, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  connectToMikrotik();

  sessionManager.begin();
  pppoeManager.begin();
  qosManager.begin(); // starts NTP sync, needed for Night Promo hour checks

  setupRoutes();
  server.begin();
  Serial.println("HTTP Server started on port 80");

  checkMikrotikConnection();
  adminAPI.logEvent("system_boot", "firmware started");

  lastSecondTick = millis(); // don't charge sessions for the boot time
  Serial.println("=== SYSTEM READY ===");
}

void loop() {
  if (inSetupMode) {
    setupModeManager.loop();
    return;
  }

  server.handleClient();
  MDNS.update();
  wifiWatchdog();
  telegramNotifier.loop(); // sends at most one queued message per call; see telegram.h

  uint32_t now = millis();

  if (now - lastSessionSave >= SESSION_SAVE_INTERVAL) {
    sessionManager.saveToSPIFFS();
    // Piggyback the activity log save on the same interval, but only
    // write it when something was actually appended since last time -
    // no point wearing the flash for an unchanged file.
    if (adminAPI.activityLogDirty()) {
      adminAPI.saveActivityLog();
    }
    if (adminAPI.todayDirty) {
      adminAPI.saveToday();
    }
    lastSessionSave = now;
  }

  if (now - lastMikrotikCheck >= HOTSPOT_CHECK_INTERVAL) {
    checkMikrotikConnection();
    lastMikrotikCheck = now;
  }

  if (now - lastDataQuery >= DATA_QUERY_INTERVAL) {
    queryTrafficStats();
    lastDataQuery = now;
  }

  // Charges the seconds that actually passed. MikroTik API calls in this
  // loop can block for seconds at a time; a fixed "1 per tick" made
  // sessions run longer than paid for whenever the router was slow.
  if (now - lastSecondTick >= 1000) {
    uint32_t elapsed = (now - lastSecondTick) / 1000;
    lastSecondTick += elapsed * 1000;
    queryActiveSessions(elapsed);
  }

  if (now - lastDailyCounterCheck >= 60000 && qosManager.timeIsSynced()) {
    time_t t = time(nullptr);
    struct tm tmNow;
    localtime_r(&t, &tmNow);
    char stamp[32];  // roomy: silences -Wformat-truncation, a date needs 11
    snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d", tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday);
    adminAPI.rollDailyCountersIfNeeded(String(stamp));
    checkSubscriptions(); // expiry needs a real clock, same cadence as the daily rollover
    lastDailyCounterCheck = now;
  }

  processCoinSlot();
  coinSlot.loop();
  updateStatusLED();

  delay(10);
}
