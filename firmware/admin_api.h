/*
 * admin_api.h - Voucher store, sales tracking, and admin settings
 * ZXHEIFI - NodeMCU ESP8266 Firmware
 *
 * Owns vouchers.json (the redeemable voucher pool, imported from the CSV
 * that vouchers/generator.py produces), a running sales counter for the
 * admin dashboard, and the small set of settings the admin page can edit
 * (Night Promo on/off override, idle timeout, auto-reboot time).
 */
#ifndef ADMIN_API_H
#define ADMIN_API_H

#include <Arduino.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <MD5Builder.h>
#include <time.h>
#include <vector>
#include "config.h"

// ---- Streaming JSON-array persistence -------------------------------------
// Vouchers, the activity log, sales history and subscribers used to be
// (de)serialized through one fixed-size document each. Past its capacity
// ArduinoJson silently drops the overflow on save and refuses the whole
// file on load - so ~100 vouchers in, a reboot left ZERO vouchers. These
// helpers handle one array element at a time with a small document, so
// memory use no longer depends on how many records there are.

template <typename Fn>
static void loadJsonArray(const char* path, size_t itemCapacity, Fn onItem) {
  if (!SPIFFS.exists(path)) return;
  File f = SPIFFS.open(path, "r");
  if (!f) return;
  if (f.find("[")) {
    DynamicJsonDocument item(itemCapacity);
    do {
      if (deserializeJson(item, f)) break;
      onItem(item.as<JsonObject>());
    } while (f.findUntil(",", "]"));
  }
  f.close();
}

// Writes to a temp file first and swaps it in, so a power cut mid-save
// leaves the previous file intact instead of a truncated one.
template <typename Fn>
static bool saveJsonArray(const char* path, size_t count, size_t itemCapacity, Fn fill) {
  String tmp = String(path) + ".tmp";
  File f = SPIFFS.open(tmp, "w");
  if (!f) return false;
  f.print('[');
  DynamicJsonDocument item(itemCapacity);
  bool first = true;
  for (size_t i = 0; i < count; i++) {
    item.clear();
    JsonObject o = item.to<JsonObject>();
    if (!fill(i, o)) continue;   // fill() returns false to skip an entry
    if (!first) f.print(',');
    first = false;
    serializeJson(item, f);
  }
  f.print(']');
  f.close();
  SPIFFS.remove(path);
  return SPIFFS.rename(tmp, path);
}

struct VoucherRecord {
  String code;
  String tier;         // "1", "2", "3", "night" - the MikroTik speed profile this voucher provisions with
  float price;
  uint32_t timeSeconds;
  uint64_t dataBytes;
  bool used;
  uint32_t validUntilEpoch; // 0 = never expires; unix timestamp otherwise - see RateProfile::validityMinutes
  uint32_t pauseWindowMinutes; // baked in at generation, so an admin editing Rate Profiles later
                                // doesn't retroactively change an already-printed ticket's grace window
};

// One admin-defined denomination: how many minutes/how much data a ₱N
// coin or ₱N voucher grants, how long it stays valid, and which of the
// 3 fixed MikroTik speed profiles (hs-default/gaming/high +
// pppoe-default/gaming/high, created once by the *_full_config.rsc
// scripts) it rides on for bandwidth. Coins and vouchers of the same
// peso amount always get the exact same minutes/data/validity/speed -
// one shared table, not two separate pricing models.
struct RateProfile {
  uint32_t pesoAmount;
  uint32_t minutes;
  uint32_t dataMb;            // 0 = unlimited data (time-only)
  uint32_t validityMinutes;   // voucher shelf-life before first use, AND the
                               // max-pause-before-forfeit window once in use
                               // (see SessionEntry::pauseWindowMinutes)
  String speedProfile;         // "1", "2", or "3"
};

// A named bandwidth cap (Settings > Speed Profiles). Rate profiles and
// subscribers pick one by id. Ids "1"/"2"/"3" map to the MikroTik profiles
// the desktop app's Config creates (hs-default/gaming/high,
// pppoe-default/gaming/high); any other id becomes zx-speed-<id> /
// zx-pppoe-<id>, created on the router when Settings is saved (see
// QoSManager::profileFor and GUIHandler::pushSpeedProfiles).
struct SpeedProfile {
  String id;
  String name;
  float downMbps;
  float upMbps;
};

// NodeMCU pin label -> GPIO for the pins the coin slot may use. D0 (no
// interrupt), D3/D4/D8 (boot-strapping pins) are deliberately absent.
static int coinPinGpio(const String& label) {
  if (label == "D1") return 5;
  if (label == "D2") return 4;
  if (label == "D5") return 14;
  if (label == "D6") return 12;
  if (label == "D7") return 13;
  return -1;
}

// A recurring account (boarding house tenant, monthly subscriber, etc)
// instead of a single-use voucher. Unlimited time+data while active;
// enforced purely by expiryEpoch, checked periodically in the .ino.
struct Subscriber {
  String username;
  String passwordHash; // MD5(salt + password)
  String salt;             // random per-account salt; "" for accounts created before salting existed
  String tier;            // "1"/"2"/"3" - which MikroTik profile to use for bandwidth
  uint32_t expiryEpoch;  // unix timestamp; access cuts off the instant this passes
  bool active;             // admin can deactivate without deleting the record
};

// A dashboard login account. "super" can change settings, manage
// subscriptions, and manage other admin accounts; "staff" can view
// stats/sales/logs, generate vouchers, and kick users.
struct AdminAccount {
  String username;
  String passwordHash; // MD5(salt + password)
  String salt;             // random per-account salt; "" for accounts created before salting existed
  String role;            // "super" or "staff"
  bool active;
};

// One entry in the rolling activity/device log (coin insertions,
// logins, admin actions, system events).
struct ActivityLogEntry {
  uint32_t epoch;
  String type;    // short code, e.g. "coin", "voucher_login", "admin_login"
  String detail;  // free-text context, e.g. a code/username or what changed
};

// Settings > Charging: one Charging Station price (v2).
struct ChargeRateCfg {
  uint32_t peso;
  uint32_t minutes;
};

// Pesos taken by one vendo - coins and charging (0 = this main unit).
struct VendoPeso {
  uint8_t id;
  uint32_t peso;
};

// One day's rolled-up sales totals, split by how it was paid.
struct DailySalesEntry {
  String dateStamp; // "YYYY-MM-DD"
  float coinRevenue;
  std::vector<VendoPeso> byVendo; // pesos per vendo, coins + charging (older files: empty = all Main)
  float chargingRevenue = 0;      // Charging Stations (v2)
  float voucherRevenue;
  float subscriptionRevenue;
  uint32_t users;
  uint64_t dataUsedBytes;
};

class AdminAPI {
public:
  bool settingNightPromoEnabled = true;
  uint16_t settingIdleTimeoutMin = IDLE_TIMEOUT_MS / 60000;
  String settingAutoRebootTime = "03:00";

  // No-code branding - applied client-side by the GUI via /api/branding.
  String brandName = "ZXHEIFI";
  String brandColor = "#2dd4c9";

  // Customer-facing sound effects/background music on login.html/status.html
  // (see mikrotik/gui/sounds/README.md) - off by default so a shop with no
  // audio files dropped in never shows the sound-toggle UI at all.
  bool soundEnabled = false;

  // Telegram sales/event notifications (see firmware/telegram.h) -
  // admin-editable so a token/chat ID change doesn't need a reflash.
  // Defaults to config.h's TELEGRAM_BOT_TOKEN/TELEGRAM_CHAT_ID
  // constants for a device that's never set these via Settings.
  bool telegramEnabled = false;
  String telegramBotToken = TELEGRAM_BOT_TOKEN;
  String telegramChatId = TELEGRAM_CHAT_ID;

  // Unified rate table - every coin denomination AND every voucher price
  // point draws from this SAME list (see profileByPeso()). Replaces the
  // old fixed Tier1/2/3 + separate linear coin-rate model: admin defines
  // as many/few denominations as their coin acceptor/voucher stock
  // actually uses, each with its own minutes/data/validity, freely (not
  // required to be a multiple of a base rate). Persisted as a JSON array
  // under the "rateProfiles" key - see loadSettings()/saveSettings().
  std::vector<RateProfile> rateProfiles;

  // MAC addresses an admin has blocked via the Active Users tab - kept
  // here purely for the dashboard's own record/display; the actual
  // enforcement is RouterOS's own /ip hotspot ip-binding type=blocked
  // entry (see MikrotikAPI::blockMac()), which rejects the client at
  // the walled-garden layer before the NodeMCU is ever involved again.
  std::vector<String> blockedMacs;

  // Named bandwidth profiles every RateProfile::speedProfile and every
  // Subscriber::tier picks from by id - see SpeedProfile. Pushed live to
  // MikroTik when Settings is saved (GUIHandler::pushSpeedProfiles).
  std::vector<SpeedProfile> speedProfiles;

  // Settings > Announcement: plain text shown on top of the customer
  // login/status pages (served via the public /api/branding).
  String announcement;

  // Settings > Coin Slot: which NodeMCU pins the coin acceptor and its
  // power relay use (baseboards differ), whether the relay switches on
  // with a HIGH or LOW signal, and pesos per pulse. Applied live.
  String coinPin = "D5";
  String relayPin = "D7";
  bool relayActiveHigh = true;
  uint16_t coinPulseValue = COIN_PULSE_VALUE_PHP;

  // Revenue is split by how it was collected so the Sales tab can show
  // a breakdown, not just one lump total.
  float coinRevenueToday = 0;
  std::vector<VendoPeso> coinByVendoToday; // pesos per vendo today (coins + charging)
  float chargingRevenueToday = 0;           // Charging Stations (v2)

  // Settings > Charging (v2) - pushed to every Charging Station box.
  std::vector<ChargeRateCfg> chargeRates;
  uint16_t chargeMaxMinutes = 180;
  String chargeLang = "tl";
  uint32_t chargeCfgVer = 0;   // bumped on change (RAM) - boxes refetch their config
  float voucherRevenueToday = 0;
  float subscriptionRevenueToday = 0;
  uint32_t usersToday = 0;
  uint64_t dataUsedTodayBytes = 0;
  String todayDateStamp; // "YYYY-MM-DD", used to auto-reset the counters above
  bool todayDirty = false; // data-used changes are saved on the periodic tick, not per packet

  // initialAdminPassword: if set (non-empty) AND this is a genuinely
  // fresh device (no admins.json yet), seeds the first super admin with
  // this password instead of the ADMIN_PASSWORD_HASH default - passed
  // in from the Setup Wizard's saved network.json (see setup_mode.h).
  // Ignored once an admins.json already exists, so it's safe to keep
  // passing the same value on every boot.
  void begin(const String& initialAdminPassword = "") {
    loadVouchers();
    loadSettings();
    loadSubscribers();
    loadAdminAccounts(initialAdminPassword);
    loadActivityLog();
    loadSalesHistory();
    loadToday();
  }

  // Every coin sale goes through here so the per-vendo split can never
  // drift from the coin total. Saved right away (a sale is money).
  void addCoinRevenue(uint8_t vendoId, uint32_t peso) {
    coinRevenueToday += peso;
    addVendoPeso(vendoId, peso);
    saveToday();
  }

  void addChargeRevenue(uint8_t vendoId, uint32_t peso) {
    chargingRevenueToday += peso;
    addVendoPeso(vendoId, peso);
    saveToday();
  }

  void addVendoPeso(uint8_t vendoId, uint32_t peso) {
    for (auto& e : coinByVendoToday) {
      if (e.id == vendoId) { e.peso += peso; return; }
    }
    coinByVendoToday.push_back({vendoId, peso});
  }

  void seedDefaultChargeRates() {
    chargeRates.clear();
    chargeRates.push_back({5, 30});
    chargeRates.push_back({10, 60});
    chargeRates.push_back({20, 150});
  }

  uint32_t coinTodayFor(uint8_t vendoId) const {
    for (auto& e : coinByVendoToday) if (e.id == vendoId) return e.peso;
    return 0;
  }

  static void writeByVendo(JsonObject o, const std::vector<VendoPeso>& list) {
    JsonArray arr = o.createNestedArray("byVendo");
    for (auto& e : list) {
      JsonObject item = arr.createNestedObject();
      item["id"] = e.id;
      item["peso"] = e.peso;
    }
  }

  static void readByVendo(JsonVariantConst src, std::vector<VendoPeso>& list) {
    list.clear();
    for (JsonObjectConst item : src.as<JsonArrayConst>()) {
      list.push_back({(uint8_t)(item["id"] | 0), (uint32_t)(item["peso"] | 0)});
    }
  }

  float revenueToday() const {
    return coinRevenueToday + voucherRevenueToday + subscriptionRevenueToday + chargingRevenueToday;
  }

  // Exact-match lookup by denomination - the heart of the unified rate
  // table. Returns nullptr if the admin hasn't defined this peso amount
  // (caller's responsibility to decide what that means - e.g. the coin
  // slot logs and discards rather than guessing at an undefined value).
  RateProfile* profileByPeso(uint32_t peso) {
    for (auto& p : rateProfiles) {
      if (p.pesoAmount == peso) return &p;
    }
    return nullptr;
  }

  // Formats one Mbps value for RouterOS's rate-limit syntax - whole
  // numbers print without a decimal point ("10M"), fractional values
  // keep one decimal ("7.5M"). String(x, 0) alone would silently round
  // 7.5 to "8", saving a different bandwidth cap than what Settings
  // actually shows/stores.
  static String mbpsToken(float mbps) {
    if (mbps == (long)mbps) return String((long)mbps) + "M";
    return String(mbps, 1) + "M";
  }

  // RouterOS rate-limit format is "rx-rate/tx-rate" (upload/download,
  // from the client's perspective) - e.g. "5M/10M". Used to push
  // tier*Down/UpMbps live to both the hotspot and pppoe MikroTik
  // profiles for that tier.
  String rateLimitStringFor(const String& speedId) {
    SpeedProfile* s = speedById(speedId);
    if (!s && !speedProfiles.empty()) s = &speedProfiles[0];
    if (!s) return "5M/10M";
    return mbpsToken(s->upMbps) + "/" + mbpsToken(s->downMbps);
  }

  SpeedProfile* speedById(const String& id) {
    for (auto& s : speedProfiles) if (s.id == id) return &s;
    return nullptr;
  }

  // First boot / upgrade: the 3 profiles the desktop app's Config creates,
  // carrying over any Mbps an older firmware saved as tier1/2/3*Mbps.
  void seedDefaultSpeedProfiles(JsonDocument* old = nullptr) {
    speedProfiles.clear();
    speedProfiles.push_back({"1", "Basic",
        old ? ((*old)["tier1DownMbps"] | (float)TIER1_DOWN_MBPS) : (float)TIER1_DOWN_MBPS,
        old ? ((*old)["tier1UpMbps"] | (float)TIER1_UP_MBPS) : (float)TIER1_UP_MBPS});
    speedProfiles.push_back({"2", "Gaming",
        old ? ((*old)["tier2DownMbps"] | (float)TIER2_DOWN_MBPS) : (float)TIER2_DOWN_MBPS,
        old ? ((*old)["tier2UpMbps"] | (float)TIER2_UP_MBPS) : (float)TIER2_UP_MBPS});
    speedProfiles.push_back({"3", "High",
        old ? ((*old)["tier3DownMbps"] | (float)TIER3_DOWN_MBPS) : (float)TIER3_DOWN_MBPS,
        old ? ((*old)["tier3UpMbps"] | (float)TIER3_UP_MBPS) : (float)TIER3_UP_MBPS});
  }

  // Salted MD5 - salt="" (every account created before salting existed)
  // reduces to plain MD5(password), so existing stored hashes keep
  // working unchanged; new accounts get a random salt from
  // randomSalt() below. MD5 is still a fast hash (not a proper
  // password KDF like bcrypt/argon2), but salting at least defeats
  // precomputed rainbow-table attacks against the stored hashes.
  static String md5Hex(const String& salt, const String& in) {
    MD5Builder md5;
    md5.begin();
    md5.add(salt + in);
    md5.calculate();
    return md5.toString();
  }

  // RANDOM_REG32 is a documented ESP8266 hardware register that
  // returns true-random bits on every read (unlike Arduino's random(),
  // a seeded PRNG) - used here instead so a new account's salt isn't
  // predictable from boot time.
  static String randomSalt() {
    char buf[9];
    snprintf(buf, sizeof(buf), "%08x", RANDOM_REG32);
    return String(buf);
  }

  // ---- Admin accounts ---------------------------------------------------

  void loadAdminAccounts(const String& initialAdminPassword = "") {
    _admins.clear();
    if (SPIFFS.exists(ADMIN_ACCOUNTS_FILE)) {
      File f = SPIFFS.open(ADMIN_ACCOUNTS_FILE, "r");
      if (f) {
        DynamicJsonDocument doc(2560);
        if (!deserializeJson(doc, f)) {
          for (JsonObject o : doc.as<JsonArray>()) {
            AdminAccount a;
            a.username = o["username"].as<String>();
            a.passwordHash = o["passwordHash"].as<String>();
            a.salt = o["salt"] | String(""); // "" for accounts saved before salting existed
            a.role = o["role"].as<String>();
            a.active = o["active"] | true;
            _admins.push_back(a);
          }
        }
        f.close();
      }
    }

    // First boot: seed a default super admin. If the Setup Wizard saved
    // a chosen password, use that (salted) instead of shipping every
    // device with the same well-known "admin"/"admin" default -
    // otherwise fall back to ADMIN_PASSWORD_HASH exactly like before,
    // so upgraded/pre-wizard deployments don't get locked out.
    if (_admins.empty()) {
      AdminAccount a;
      a.username = "admin";
      if (initialAdminPassword.length()) {
        a.salt = randomSalt();
        a.passwordHash = md5Hex(a.salt, initialAdminPassword);
      } else {
        a.passwordHash = ADMIN_PASSWORD_HASH;
      }
      a.role = "super";
      a.active = true;
      _admins.push_back(a);
      saveAdminAccounts();
    }
  }

  void saveAdminAccounts() {
    DynamicJsonDocument doc(2560);
    JsonArray arr = doc.to<JsonArray>();
    for (auto& a : _admins) {
      JsonObject o = arr.createNestedObject();
      o["username"] = a.username;
      o["passwordHash"] = a.passwordHash;
      o["salt"] = a.salt;
      o["role"] = a.role;
      o["active"] = a.active;
    }
    File f = SPIFFS.open(ADMIN_ACCOUNTS_FILE, "w");
    if (!f) return;
    serializeJson(doc, f);
    f.close();
  }

  AdminAccount* findAdminAccount(const String& username) {
    for (auto& a : _admins) {
      if (a.username == username) return &a;
    }
    return nullptr;
  }

  AdminAccount* checkAdminLogin(const String& username, const String& password) {
    AdminAccount* a = findAdminAccount(username);
    if (!a || !a->active) return nullptr;
    if (a->passwordHash != md5Hex(a->salt, password)) return nullptr;
    return a;
  }

  uint16_t activeSuperAdminCount() {
    uint16_t n = 0;
    for (auto& a : _admins) if (a.active && a.role == "super") n++;
    return n;
  }

  bool addAdminAccount(const String& username, const String& password, const String& role) {
    if (findAdminAccount(username) || _admins.size() >= MAX_ADMIN_ACCOUNTS) return false;
    if (role != "super" && role != "staff") return false;
    AdminAccount a;
    a.username = username;
    a.salt = randomSalt();
    a.passwordHash = md5Hex(a.salt, password);
    a.role = role;
    a.active = true;
    _admins.push_back(a);
    saveAdminAccounts();
    return true;
  }

  // Refuses to disable/demote the last active super admin, so nobody
  // can lock themselves (or everyone) out of the settings/subscriptions
  // that only a super admin can reach.
  bool setAdminAccountActive(const String& username, bool active) {
    AdminAccount* a = findAdminAccount(username);
    if (!a) return false;
    if (!active && a->role == "super" && a->active && activeSuperAdminCount() <= 1) return false;
    a->active = active;
    saveAdminAccounts();
    return true;
  }

  uint32_t adminAccountCount() { return _admins.size(); }
  AdminAccount& adminAccountAt(uint32_t i) { return _admins[i]; }

  // ---- Activity log -----------------------------------------------------

  void loadActivityLog() {
    _activityLog.clear();
    loadJsonArray(ACTIVITY_LOG_FILE, 384, [this](JsonObject o) {
      if (_activityLog.size() >= MAX_ACTIVITY_LOG_ENTRIES) _activityLog.erase(_activityLog.begin());
      ActivityLogEntry e;
      e.epoch = o["epoch"] | 0;
      e.type = o["type"].as<String>();
      e.detail = o["detail"].as<String>();
      _activityLog.push_back(e);
    });
  }

  void saveActivityLog() {
    saveJsonArray(ACTIVITY_LOG_FILE, _activityLog.size(), 384, [this](size_t i, JsonObject o) {
      o["epoch"] = _activityLog[i].epoch;
      o["type"] = _activityLog[i].type;
      o["detail"] = _activityLog[i].detail;
      return true;
    });
    _activityLogDirty = false;
  }

  // In-RAM only until the next periodic saveActivityLog() call (see the
  // .ino's main loop) - logging every coin pulse straight to flash would
  // wear it out fast.
  void logEvent(const String& type, const String& detail) {
    ActivityLogEntry e;
    e.epoch = (uint32_t)time(nullptr);
    e.type = type;
    e.detail = detail;
    _activityLog.push_back(e);
    if (_activityLog.size() > MAX_ACTIVITY_LOG_ENTRIES) {
      _activityLog.erase(_activityLog.begin());
    }
    _activityLogDirty = true;
  }

  bool activityLogDirty() const { return _activityLogDirty; }
  uint32_t activityLogCount() { return _activityLog.size(); }
  // Newest first, matching how a log is normally read.
  ActivityLogEntry& activityLogAt(uint32_t iFromNewest) {
    return _activityLog[_activityLog.size() - 1 - iFromNewest];
  }

  // ---- Sales history ------------------------------------------------------

  void loadSalesHistory() {
    _salesHistory.clear();
    loadJsonArray(SALES_HISTORY_FILE, 1024, [this](JsonObject o) {
      if (_salesHistory.size() >= MAX_SALES_HISTORY_DAYS) _salesHistory.erase(_salesHistory.begin());
      DailySalesEntry e;
      e.dateStamp = o["dateStamp"].as<String>();
      e.coinRevenue = o["coinRevenue"] | 0.0f;
      readByVendo(o["byVendo"], e.byVendo);
      e.chargingRevenue = o["chargingRevenue"] | 0.0f;
      e.voucherRevenue = o["voucherRevenue"] | 0.0f;
      e.subscriptionRevenue = o["subscriptionRevenue"] | 0.0f;
      e.users = o["users"] | 0;
      e.dataUsedBytes = o["dataUsedBytes"].as<uint64_t>();
      _salesHistory.push_back(e);
    });
  }

  void saveSalesHistory() {
    saveJsonArray(SALES_HISTORY_FILE, _salesHistory.size(), 1024, [this](size_t i, JsonObject o) {
      DailySalesEntry& e = _salesHistory[i];
      o["dateStamp"] = e.dateStamp;
      o["coinRevenue"] = e.coinRevenue;
      writeByVendo(o, e.byVendo);
      o["chargingRevenue"] = e.chargingRevenue;
      o["voucherRevenue"] = e.voucherRevenue;
      o["subscriptionRevenue"] = e.subscriptionRevenue;
      o["users"] = e.users;
      o["dataUsedBytes"] = (double)e.dataUsedBytes;
      return true;
    });
  }

  uint32_t salesHistoryCount() { return _salesHistory.size(); }
  // Newest first.
  DailySalesEntry& salesHistoryAt(uint32_t iFromNewest) {
    return _salesHistory[_salesHistory.size() - 1 - iFromNewest];
  }

  // ---- Vouchers -----------------------------------------------------------

  // Only UNUSED vouchers are kept (in RAM and on flash): a redeemed code
  // gets the same "invalid_or_used_voucher" answer whether or not it's
  // still on file, and the live session - not the voucher - is what a
  // reconnect resumes from. Capped at MAX_VOUCHERS so the store can't
  // eat the ESP8266's heap; import more as they sell.
  void loadVouchers() {
    _vouchers.clear();
    loadJsonArray(VOUCHERS_FILE, 384, [this](JsonObject o) {
      if (_vouchers.size() >= MAX_VOUCHERS || (o["used"] | false)) return;
      VoucherRecord v;
      v.code = o["code"].as<String>();
      v.tier = o["tier"].as<String>();
      v.price = o["price"] | 0.0f;
      v.timeSeconds = o["timeSeconds"] | 0;
      v.dataBytes = o["dataBytes"].as<uint64_t>();
      v.used = false;
      v.validUntilEpoch = o["validUntilEpoch"] | 0;
      v.pauseWindowMinutes = o["pauseWindowMinutes"] | (uint32_t)MAX_PAUSE_MINUTES;
      _vouchers.push_back(v);
    });
  }

  void saveVouchers() {
    saveJsonArray(VOUCHERS_FILE, _vouchers.size(), 384, [this](size_t i, JsonObject o) {
      VoucherRecord& v = _vouchers[i];
      if (v.used) return false;
      o["code"] = v.code;
      o["tier"] = v.tier;
      o["price"] = v.price;
      o["timeSeconds"] = v.timeSeconds;
      o["dataBytes"] = (double)v.dataBytes;
      o["validUntilEpoch"] = v.validUntilEpoch;
      o["pauseWindowMinutes"] = v.pauseWindowMinutes;
      return true;
    });
  }

  // Drops a voucher from RAM once its session is safely provisioned (it
  // was already left out of vouchers.json by saveVouchers()). Until then
  // it stays in memory so a failed MikroTik provision can roll it back.
  void forgetUsedVoucher(const String& code) {
    for (size_t i = 0; i < _vouchers.size(); i++) {
      if (_vouchers[i].code == code && _vouchers[i].used) {
        _vouchers.erase(_vouchers.begin() + i);
        return;
      }
    }
  }

  // Undoes redeemVoucher() when provisioning on MikroTik failed.
  void rollbackVoucher(const String& code) {
    VoucherRecord* v = findVoucher(code);
    if (!v) return;
    v->used = false;
    if (v->price <= voucherRevenueToday) voucherRevenueToday -= v->price;
    if (usersToday) usersToday--;
    saveVouchers();
    saveToday();
  }

  // Parses the CSV produced by vouchers/generator.py:
  // code,tier,label,price_php,time_seconds,data_bytes,generated_at,used,expiry_epoch,pause_window_min
  // Columns 9 (expiry_epoch) and 10 (pause_window_min) are optional for
  // backward compatibility with older CSVs; missing = no expiry / falls
  // back to MAX_PAUSE_MINUTES. Unknown/duplicate codes are skipped; new
  // ones are appended as unused.
  uint16_t importCsv(const String& csv) {
    uint16_t imported = 0;
    int lineStart = 0;
    bool headerSkipped = false;
    while (lineStart < (int)csv.length() && _vouchers.size() < MAX_VOUCHERS) {
      int lineEnd = csv.indexOf('\n', lineStart);
      if (lineEnd < 0) lineEnd = csv.length();
      String line = csv.substring(lineStart, lineEnd);
      line.trim();
      lineStart = lineEnd + 1;
      if (!line.length()) continue;
      if (!headerSkipped) { headerSkipped = true; continue; }

      String cols[10];
      int col = 0, start = 0;
      for (int i = 0; i <= (int)line.length() && col < 10; i++) {
        if (i == (int)line.length() || line[i] == ',') {
          cols[col++] = line.substring(start, i);
          start = i + 1;
        }
      }
      if (col < 6) continue;
      if (findVoucher(cols[0])) continue; // duplicate code, skip

      VoucherRecord v;
      v.code = cols[0];
      v.tier = cols[1];
      v.price = cols[3].toFloat();
      v.timeSeconds = cols[4].toInt();
      v.dataBytes = strtoull(cols[5].c_str(), nullptr, 10);
      v.used = false;
      v.validUntilEpoch = (col >= 9) ? strtoul(cols[8].c_str(), nullptr, 10) : 0;
      v.pauseWindowMinutes = (col >= 10) ? strtoul(cols[9].c_str(), nullptr, 10) : MAX_PAUSE_MINUTES;
      _vouchers.push_back(v);
      imported++;
    }
    if (imported) saveVouchers();
    return imported;
  }

  // On-device quick generation (no printer needed) for a handful of
  // vouchers at a time, using the *current* Rate Profile for this peso
  // amount. For bulk printed tickets with QR codes, use
  // vouchers/generator.py and importCsv() instead.
  std::vector<String> generateVouchers(uint32_t pesoAmount, uint8_t count) {
    RateProfile* p = profileByPeso(pesoAmount);
    if (!p) return {};
    uint32_t timeSeconds = p->minutes * 60UL;
    uint64_t dataBytes = p->dataMb > 0 ? (uint64_t)p->dataMb * 1024UL * 1024UL : UNLIMITED_BYTES;
    // Without a synced clock time() is ~1970+uptime, so an expiry computed
    // from it lands in 1970 and every voucher reads as expired the moment
    // NTP catches up (e.g. vouchers made while the shop's internet is down).
    // Unknown time -> no expiry, rather than instant expiry.
    uint32_t now = (uint32_t)time(nullptr);
    bool synced = clockSynced();
    uint32_t validUntil = (p->validityMinutes > 0 && synced) ? now + p->validityMinutes * 60UL : 0;
    if (p->validityMinutes > 0 && !synced) {
      logEvent("voucher_no_expiry", "clock not synced - vouchers generated without an expiry date");
    }

    std::vector<String> codes;
    for (uint8_t i = 0; i < count && _vouchers.size() < MAX_VOUCHERS; i++) {
      // 8 characters fit the ESP8266 String's inline buffer (no heap
      // allocation per voucher) and are easy to type. No 0/O/1/I.
      static const char* alphabet = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
      String code;
      do {
        code = "ZX";
        for (uint8_t c = 0; c < 6; c++) code += alphabet[RANDOM_REG32 % 32];
      } while (findVoucher(code));

      VoucherRecord v;
      v.code = code;
      v.tier = p->speedProfile;
      v.price = (float)pesoAmount;
      v.timeSeconds = timeSeconds;
      v.dataBytes = dataBytes;
      v.used = false;
      v.validUntilEpoch = validUntil;
      v.pauseWindowMinutes = p->validityMinutes > 0 ? p->validityMinutes : MAX_PAUSE_MINUTES;
      _vouchers.push_back(v);
      codes.push_back(code);
    }
    if (codes.size()) saveVouchers();
    return codes;
  }

  VoucherRecord* findVoucher(const String& code) {
    for (auto& v : _vouchers) {
      if (v.code == code) return &v;
    }
    return nullptr;
  }

  bool isVoucherExpired(const VoucherRecord& v) {
    return v.validUntilEpoch != 0 && (uint32_t)time(nullptr) > v.validUntilEpoch;
  }

  // Marks a voucher used and records the sale for the dashboard. Returns
  // false if the code doesn't exist, was already redeemed, or has
  // passed its validity date (an expired-but-unused voucher is treated
  // the same as an invalid one - no distinct error, so a customer can't
  // fish for which codes used to be valid).
  bool redeemVoucher(const String& code, VoucherRecord& out) {
    VoucherRecord* v = findVoucher(code);
    if (!v || v->used || isVoucherExpired(*v)) return false;
    v->used = true;
    saveVouchers();
    out = *v;
    voucherRevenueToday += v->price;
    usersToday++;
    saveToday();
    return true;
  }

  uint32_t voucherCount() { return _vouchers.size(); }
  uint32_t unusedVoucherCount() {
    uint32_t n = 0;
    for (auto& v : _vouchers) if (!v.used) n++;
    return n;
  }

  // ---- Settings -------------------------------------------------------

  void loadSettings() {
    seedDefaultChargeRates();
    if (!SPIFFS.exists(CONFIG_FILE)) { seedDefaultRateProfiles(); seedDefaultSpeedProfiles(); return; }
    File f = SPIFFS.open(CONFIG_FILE, "r");
    if (!f) { seedDefaultRateProfiles(); seedDefaultSpeedProfiles(); return; }
    // Sized for MAX_RATE_PROFILES + MAX_BLOCKED_MACS. At 3072 a shop with
    // ~15 profiles or a few dozen blocked phones overflowed it, the parse
    // failed, and every setting (profiles included) silently reset to
    // defaults on the next boot.
    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, f)) { f.close(); seedDefaultRateProfiles(); seedDefaultSpeedProfiles(); return; }
    f.close();
    settingNightPromoEnabled = doc["nightPromoEnabled"] | true;
    settingIdleTimeoutMin = doc["idleTimeoutMin"] | (IDLE_TIMEOUT_MS / 60000);
    settingAutoRebootTime = doc["autoRebootTime"] | String("03:00");
    brandName = doc["brandName"] | String("ZXHEIFI");
    brandColor = doc["brandColor"] | String("#2dd4c9");
    soundEnabled = doc["soundEnabled"] | false;
    speedProfiles.clear();
    if (doc.containsKey("speedProfiles")) {
      for (JsonObject o : doc["speedProfiles"].as<JsonArray>()) {
        if (speedProfiles.size() >= MAX_SPEED_PROFILES) break;
        speedProfiles.push_back({o["id"] | String("1"), o["name"] | String("Speed"),
                                 o["downMbps"] | 5.0f, o["upMbps"] | 2.0f});
      }
    }
    if (speedProfiles.empty()) seedDefaultSpeedProfiles(&doc);
    announcement = doc["announcement"] | String("");
    coinPin = doc["coinPin"] | String("D5");
    relayPin = doc["relayPin"] | String("D7");
    relayActiveHigh = doc["relayActiveHigh"] | true;
    coinPulseValue = doc["coinPulseValue"] | (uint16_t)COIN_PULSE_VALUE_PHP;
    telegramEnabled = doc["telegramEnabled"] | false;
    telegramBotToken = doc["telegramBotToken"] | String(TELEGRAM_BOT_TOKEN);
    telegramChatId = doc["telegramChatId"] | String(TELEGRAM_CHAT_ID);

    rateProfiles.clear();
    if (doc.containsKey("rateProfiles")) {
      for (JsonObject o : doc["rateProfiles"].as<JsonArray>()) {
        RateProfile p;
        p.pesoAmount = o["pesoAmount"] | 0;
        p.minutes = o["minutes"] | 0;
        p.dataMb = o["dataMb"] | 0;
        p.validityMinutes = o["validityMinutes"] | 0;
        p.speedProfile = o["speedProfile"] | String("1");
        rateProfiles.push_back(p);
      }
    } else {
      // Upgrading from a pre-Rate-Profiles device: seed 3 entries from
      // the old fixed Tier1/2/3 constants so nothing silently resets.
      seedDefaultRateProfiles();
    }

    blockedMacs.clear();
    for (JsonVariant v : doc["blockedMacs"].as<JsonArray>()) {
      blockedMacs.push_back(v.as<String>());
    }

    if (doc.containsKey("chargeRates")) {
      chargeRates.clear();
      for (JsonObject o : doc["chargeRates"].as<JsonArray>()) {
        if (chargeRates.size() >= 10) break;
        uint32_t peso = o["peso"] | 0, minutes = o["minutes"] | 0;
        if (peso && minutes) chargeRates.push_back({peso, minutes});
      }
    }
    chargeMaxMinutes = doc["chargeMaxMinutes"] | 180;
    chargeLang = doc["chargeLang"] | String("tl");
  }

  // Old Tier1/2/3 compile-time defaults, converted to the new unified
  // rate table's units (minutes, not hours; no separate coin rate).
  void seedDefaultRateProfiles() {
    rateProfiles.clear();
    rateProfiles.push_back({(uint32_t)TIER1_PRICE_PHP, (uint32_t)(TIER1_TIME / 60), (uint32_t)(TIER1_DATA / (1024UL * 1024UL)), 1440, "1"});
    rateProfiles.push_back({(uint32_t)TIER2_PRICE_PHP, (uint32_t)(TIER2_TIME / 60), (uint32_t)(TIER2_DATA / (1024UL * 1024UL)), 1440, "2"});
    rateProfiles.push_back({(uint32_t)TIER3_PRICE_PHP, (uint32_t)(TIER3_TIME / 60), (uint32_t)(TIER3_DATA / (1024UL * 1024UL)), 1440, "3"});
  }

  bool saveSettings() {
    DynamicJsonDocument doc(6144);
    doc["nightPromoEnabled"] = settingNightPromoEnabled;
    doc["idleTimeoutMin"] = settingIdleTimeoutMin;
    doc["autoRebootTime"] = settingAutoRebootTime;
    doc["brandName"] = brandName;
    doc["brandColor"] = brandColor;
    doc["soundEnabled"] = soundEnabled;
    JsonArray speedArr = doc.createNestedArray("speedProfiles");
    for (auto& s : speedProfiles) {
      JsonObject o = speedArr.createNestedObject();
      o["id"] = s.id;
      o["name"] = s.name;
      o["downMbps"] = s.downMbps;
      o["upMbps"] = s.upMbps;
    }
    doc["announcement"] = announcement;
    doc["coinPin"] = coinPin;
    doc["relayPin"] = relayPin;
    doc["relayActiveHigh"] = relayActiveHigh;
    doc["coinPulseValue"] = coinPulseValue;
    doc["telegramEnabled"] = telegramEnabled;
    doc["telegramBotToken"] = telegramBotToken;
    doc["telegramChatId"] = telegramChatId;

    JsonArray profilesArr = doc.createNestedArray("rateProfiles");
    for (auto& p : rateProfiles) {
      JsonObject o = profilesArr.createNestedObject();
      o["pesoAmount"] = p.pesoAmount;
      o["minutes"] = p.minutes;
      o["dataMb"] = p.dataMb;
      o["validityMinutes"] = p.validityMinutes;
      o["speedProfile"] = p.speedProfile;
    }
    JsonArray blockedArr = doc.createNestedArray("blockedMacs");
    for (auto& m : blockedMacs) blockedArr.add(m);
    JsonArray chargeArr = doc.createNestedArray("chargeRates");
    for (auto& r : chargeRates) {
      JsonObject o = chargeArr.createNestedObject();
      o["peso"] = r.peso;
      o["minutes"] = r.minutes;
    }
    doc["chargeMaxMinutes"] = chargeMaxMinutes;
    doc["chargeLang"] = chargeLang;

    // Out of heap (capacity 0) or too big: writing now would leave a
    // truncated config.json that resets every setting on the next boot.
    if (doc.capacity() == 0 || doc.overflowed()) {
      Serial.println("saveSettings: not enough memory - config.json left unchanged");
      return false;
    }
    // Temp file + rename, so a power cut mid-write keeps the old file.
    String tmp = String(CONFIG_FILE) + ".tmp";
    File f = SPIFFS.open(tmp, "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    SPIFFS.remove(CONFIG_FILE);
    return SPIFFS.rename(tmp, CONFIG_FILE);
  }

  // ---- Blocked clients --------------------------------------------------

  bool isMacBlocked(const String& mac) {
    for (auto& m : blockedMacs) if (m.equalsIgnoreCase(mac)) return true;
    return false;
  }

  void addBlockedMac(const String& mac) {
    if (isMacBlocked(mac)) return;
    if (blockedMacs.size() >= MAX_BLOCKED_MACS) blockedMacs.erase(blockedMacs.begin()); // oldest block drops off
    blockedMacs.push_back(mac);
    saveSettings();
  }

  void removeBlockedMac(const String& mac) {
    for (size_t i = 0; i < blockedMacs.size(); i++) {
      if (blockedMacs[i].equalsIgnoreCase(mac)) {
        blockedMacs.erase(blockedMacs.begin() + i);
        saveSettings();
        return;
      }
    }
  }

  // True once NTP has set a real date. Before that time() counts from
  // 1970, and any expiry computed from it is decades in the past.
  static bool clockSynced() { return (uint32_t)time(nullptr) > 1700000000UL; }

  // ---- Subscribers ----------------------------------------------------

  void loadSubscribers() {
    _subscribers.clear();
    loadJsonArray(SUBSCRIBERS_FILE, 512, [this](JsonObject o) {
      if (_subscribers.size() >= MAX_SUBSCRIBERS) return;
      Subscriber s;
      s.username = o["username"].as<String>();
      s.passwordHash = o["passwordHash"].as<String>();
      s.salt = o["salt"] | String(""); // "" for accounts saved before salting existed
      s.tier = o["tier"].as<String>();
      s.expiryEpoch = o["expiryEpoch"] | 0;
      s.active = o["active"] | true;
      _subscribers.push_back(s);
    });
  }

  void saveSubscribers() {
    saveJsonArray(SUBSCRIBERS_FILE, _subscribers.size(), 512, [this](size_t i, JsonObject o) {
      Subscriber& s = _subscribers[i];
      o["username"] = s.username;
      o["passwordHash"] = s.passwordHash;
      o["salt"] = s.salt;
      o["tier"] = s.tier;
      o["expiryEpoch"] = s.expiryEpoch;
      o["active"] = s.active;
      return true;
    });
  }

  Subscriber* findSubscriber(const String& username) {
    for (auto& s : _subscribers) {
      if (s.username == username) return &s;
    }
    return nullptr;
  }

  // Returns false if the username already exists or the table is full.
  // pricePaid is purely for the Sales tab's records - subscriptions are
  // billed informally (admin collects payment in person), so the admin
  // just types in what was actually collected.
  bool addSubscriber(const String& username, const String& password,
                      const String& tier, uint16_t initialDays, float pricePaid = 0) {
    if (findSubscriber(username) || _subscribers.size() >= MAX_SUBSCRIBERS) return false;
    Subscriber s;
    s.username = username;
    s.salt = randomSalt();
    s.passwordHash = md5Hex(s.salt, password);
    s.tier = tier;
    s.expiryEpoch = (uint32_t)time(nullptr) + (uint32_t)initialDays * 86400UL;
    s.active = true;
    _subscribers.push_back(s);
    saveSubscribers();
    subscriptionRevenueToday += pricePaid;
    saveToday();
    return true;
  }

  // Extends from the later of "now" or the current expiry, so renewing
  // early doesn't waste the days still remaining on the old period.
  bool renewSubscriber(const String& username, uint16_t addDays, float pricePaid = 0) {
    Subscriber* s = findSubscriber(username);
    if (!s) return false;
    uint32_t now = (uint32_t)time(nullptr);
    uint32_t base = (s->expiryEpoch > now) ? s->expiryEpoch : now;
    s->expiryEpoch = base + (uint32_t)addDays * 86400UL;
    s->active = true;
    saveSubscribers();
    subscriptionRevenueToday += pricePaid;
    saveToday();
    return true;
  }

  bool setSubscriberActive(const String& username, bool active) {
    Subscriber* s = findSubscriber(username);
    if (!s) return false;
    s->active = active;
    saveSubscribers();
    return true;
  }

  // Verifies credentials AND that the account is active and unexpired.
  // Returns nullptr on any failure - callers shouldn't distinguish "wrong
  // password" from "expired" in the response (avoids leaking which
  // usernames exist).
  Subscriber* checkSubscriberLogin(const String& username, const String& password) {
    Subscriber* s = findSubscriber(username);
    if (!s || !s->active) return nullptr;
    if (s->passwordHash != md5Hex(s->salt, password)) return nullptr;
    if (isSubscriberExpired(*s)) return nullptr;
    return s;
  }

  bool isSubscriberExpired(const Subscriber& s) {
    return (uint32_t)time(nullptr) >= s.expiryEpoch;
  }

  uint32_t subscriberCount() { return _subscribers.size(); }
  Subscriber& subscriberAt(uint32_t i) { return _subscribers[i]; }

  // Rolls today's totals into sales history and resets the daily
  // counters when the wall-clock date changes. The very first call
  // after boot just sets todayDateStamp without pushing a history
  // entry (there's nothing to roll from yet).
  void rollDailyCountersIfNeeded(const String& currentDateStamp) {
    if (todayDateStamp == currentDateStamp) return;

    // First real date since boot and no saved day yet: whatever was sold
    // since boot IS today's - just name the day. (This used to zero the
    // counters, wiping every sale made before the clock was known.)
    if (!todayDateStamp.length()) {
      todayDateStamp = currentDateStamp;
      saveToday();
      return;
    }

    {
      DailySalesEntry e;
      e.dateStamp = todayDateStamp;
      e.coinRevenue = coinRevenueToday;
      e.byVendo = coinByVendoToday;
      e.chargingRevenue = chargingRevenueToday;
      e.voucherRevenue = voucherRevenueToday;
      e.subscriptionRevenue = subscriptionRevenueToday;
      e.users = usersToday;
      e.dataUsedBytes = dataUsedTodayBytes;
      _salesHistory.push_back(e);
      if (_salesHistory.size() > MAX_SALES_HISTORY_DAYS) {
        _salesHistory.erase(_salesHistory.begin());
      }
      saveSalesHistory();
    }

    todayDateStamp = currentDateStamp;
    coinRevenueToday = 0;
    coinByVendoToday.clear();
    chargingRevenueToday = 0;
    voucherRevenueToday = 0;
    subscriptionRevenueToday = 0;
    usersToday = 0;
    dataUsedTodayBytes = 0;
    saveToday();
  }

  // Today's running totals live in RAM and used to vanish on every
  // reboot (e.g. moving the NodeMCU to its baseboard) - kept in a tiny
  // file, rewritten on every sale and on the periodic tick.
  void saveToday() {
    DynamicJsonDocument doc(1024);
    doc["dateStamp"] = todayDateStamp;
    doc["coin"] = coinRevenueToday;
    writeByVendo(doc.as<JsonObject>(), coinByVendoToday);
    doc["charging"] = chargingRevenueToday;
    doc["voucher"] = voucherRevenueToday;
    doc["subscription"] = subscriptionRevenueToday;
    doc["users"] = usersToday;
    doc["dataUsedBytes"] = (double)dataUsedTodayBytes;
    File f = SPIFFS.open(TODAY_SALES_FILE, "w");
    if (!f) return;
    serializeJson(doc, f);
    f.close();
    todayDirty = false;
  }

  void loadToday() {
    if (!SPIFFS.exists(TODAY_SALES_FILE)) return;
    File f = SPIFFS.open(TODAY_SALES_FILE, "r");
    if (!f) return;
    DynamicJsonDocument doc(1024);
    bool bad = (bool)deserializeJson(doc, f);
    f.close();
    if (bad) return;
    todayDateStamp = doc["dateStamp"] | String("");
    coinRevenueToday = doc["coin"] | 0.0f;
    readByVendo(doc["byVendo"], coinByVendoToday);
    chargingRevenueToday = doc["charging"] | 0.0f;
    voucherRevenueToday = doc["voucher"] | 0.0f;
    subscriptionRevenueToday = doc["subscription"] | 0.0f;
    usersToday = doc["users"] | 0;
    dataUsedTodayBytes = doc["dataUsedBytes"].as<uint64_t>();
  }

private:
  std::vector<VoucherRecord> _vouchers;
  std::vector<Subscriber> _subscribers;
  std::vector<AdminAccount> _admins;
  std::vector<ActivityLogEntry> _activityLog;
  std::vector<DailySalesEntry> _salesHistory;
  bool _activityLogDirty = false;
  uint32_t _genCounter = 0;
};

#endif // ADMIN_API_H
