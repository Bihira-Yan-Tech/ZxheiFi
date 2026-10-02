/*
 * config.h — ZXHEIFI NODEMCU CONFIGURATION
 *
 * This file contains all configurable constants for the NodeMCU firmware.
 * Edit these values before compiling to match your network.
 */

#ifndef CONFIG_H
#define CONFIG_H

// Reported by /api/health. mikrotik/gui/script.js (REQUIRED_FIRMWARE)
// warns the admin when the NodeMCU runs an older build than the pages
// uploaded to the router - a mismatch made Settings silently misbehave.
#define FIRMWARE_VERSION "2.0.0-dev"

#include "platform.h"   // NodeMCU or ESP32 - see platform.h

// ============================================================================
// NETWORK CONFIGURATION
// ============================================================================

#define NODEMCU_IP        "10.0.0.254"     // Static IP for NodeMCU
#define NODEMCU_GATEWAY   "10.0.0.1"       // MikroTik LAN IP
#define NODEMCU_SUBNET    "255.255.255.0"  // /24 subnet
#define NODEMCU_DNS       "8.8.8.8"        // DNS server

// MikroTik API credentials
#define MIKROTIK_API_USER "zxheifi-api"
#define MIKROTIK_API_PASS "changeme123"    // Update to match mikrotik config
#define MIKROTIK_API_PORT 8728

// WiFi credentials (NodeMCU connects as client to MikroTik WiFi)
#define WIFI_SSID         "ZXHEIFI"
#define WIFI_PASSWORD     "yourwifipassword123"  // Must match MikroTik AP

// ============================================================================
// VOUCHER & PRICING CONFIGURATION
// ============================================================================

// Pricing tiers (in PHP) — edit via admin page
#define TIER1_PRICE       "10"   // ₱10 = 1hr / 500MB
#define TIER2_PRICE       "20"   // ₱20 = 2hr / 1.2GB + Gaming QoS
#define TIER3_PRICE       "30"   // ₱30 = 3hr / 2GB + High Priority

// Numeric mirrors of the above, used by coin-slot credit math.
#define TIER1_PRICE_PHP   10
#define TIER2_PRICE_PHP   20
#define TIER3_PRICE_PHP   30

// Default per-tier bandwidth caps (Mbps) - mirrors the rate-limit=
// values the .rsc scripts create on hs-default/gaming/high and
// pppoe-default/gaming/high. AdminAPI's tier*Down/UpMbps settings
// start from these and, once an admin edits them, get pushed live to
// those same MikroTik profiles - see AdminAPI::rateLimitStringFor().
#define TIER1_DOWN_MBPS   10
#define TIER1_UP_MBPS     5
#define TIER2_DOWN_MBPS   30
#define TIER2_UP_MBPS     10
#define TIER3_DOWN_MBPS   50
#define TIER3_UP_MBPS     20

// ============================================================================
// COIN SLOT
// ============================================================================
#define COIN_PULSE_VALUE_PHP 1     // Pesos represented by one pulse (set to match your acceptor)
#define COIN_DEBOUNCE_MS     300   // Pulse train considered finished after this much silence

// Defensive pulse validation - see docs/11-coin-acceptor-wiring.md.
// Real coin acceptor pulses are electromechanical/opto-isolated and land
// tens of ms apart at minimum; anything faster is almost always contact
// bounce or electrical noise, not a second coin.
#define MIN_PULSE_INTERVAL_US 5000  // 5ms - pulses closer together than this are ignored
// A single coin (even PHP20, the highest proportional-pulse denomination
// this system prices) should never plausibly produce more pulses than
// this in one debounce window. A burst past it gets discarded and
// logged rather than credited - more likely a malfunction, a shorted
// line, or someone jiggling the mechanism than PHP50+ of real coins.
#define MAX_PLAUSIBLE_PULSES  50

// Coin session (see coin_slot.h): the customer taps "Insert Coin" on the
// login page, which reserves the slot for their phone and switches the
// relay on. Coins then credit THAT customer until they tap Done or the
// slot sits idle this long - then the credit is turned into a session
// automatically, so a customer who walks off never loses their money.
#define COIN_IDLE_TIMEOUT_MS    45000   // no coin for this long after the last one -> finish
#define COIN_NO_COIN_TIMEOUT_MS 60000   // reserved but never paid -> release the slot
#define COIN_ORPHAN_WINDOW_MS   60000   // a coin dropped before tapping Insert Coin counts if tapped within this
#define COIN_RESULT_KEEP_MS     600000  // a finished code stays retrievable this long (page reload, etc.)

// Data limits in bytes (1MB = 1048576 bytes)
#define TIER1_DATA        500UL * 1048576    // 500MB
#define TIER2_DATA        1200UL * 1048576   // 1.2GB
#define TIER3_DATA        2000UL * 1048576   // 2GB

// Time limits in seconds (1hr = 3600s)
#define TIER1_TIME        3600    // 1 hour
#define TIER2_TIME        7200    // 2 hours
#define TIER3_TIME        10800   // 3 hours

// Night promo hours (12AM-6AM) — 24hr format
#define NIGHT_PROMO_START_HOUR  0    // 12 AM
#define NIGHT_PROMO_END_HOUR    6    // 6 AM
#define NIGHT_PROMO_PRICE       "5"  // ₱5/hour
#define NIGHT_PROMO_DATA_MULT   2    // Double data

// ============================================================================
// TRACKING INTERVALS
// ============================================================================
#define SESSION_SAVE_INTERVAL 30000   // 30s — SPIFFS save for recovery
#define HOTSPOT_CHECK_INTERVAL 30000 // 30s — MikroTik ping check
#define DATA_QUERY_INTERVAL    10000  // 10s — Traffic stats query
#define SESSION_QUERY_INTERVAL 30000  // 30s — Active sessions query

// ============================================================================
// IDLE & WARNING SETTINGS
// ============================================================================
#define IDLE_TIMEOUT_MS   300000      // 5 min idle before pause
#define WARNING_TIME_MS   300000      // Warn at 5 min remaining
#define FINAL_WARNING_MS  60000       // Final warn at 1 min
#define IDLE_ACTIVITY_BYTES 51200     // <50KB moved between polls counts as idle

// ============================================================================
// SESSION TABLE
// ============================================================================
#define MAX_SESSIONS      20          // Concurrent active sessions tracked in RAM
#define MAX_VOUCHERS      200         // Unused vouchers stored on the device at once (heap-bound); import more as they sell
#define MAX_RATE_PROFILES 20          // Rate Profiles (one per coin/voucher price) - keeps config.json within its load buffer
#define MAX_BLOCKED_MACS  30          // Blocked devices remembered in config.json
#define MAX_SPEED_PROFILES 8          // Named bandwidth profiles (Settings > Speed Profiles)
#define MAX_ANNOUNCEMENT_LEN 300      // Settings > Announcement, shown on top of the customer portal

// Sentinel values standing in for "unlimited" - used when a Rate
// Profile's dataMb is 0 (time-only) and for subscriptions (unlimited
// both ways). Large enough to never realistically run out, small
// enough to avoid overflow when added to during Extend.
#define UNLIMITED_SECONDS  (uint32_t)0xFFFFFFF0
#define UNLIMITED_BYTES    (uint64_t)0xFFFFFFFFFFF00000ULL

// ============================================================================
// PAUSE / RESUME
// ============================================================================
#define MAX_PAUSE_MINUTES  120        // Auto-expire a session paused longer than this

// ============================================================================
// SUBSCRIPTIONS
// ============================================================================
#define MAX_SUBSCRIBERS    50         // Subscriber accounts tracked in RAM

// ============================================================================
// MULTI-ADMIN
// ============================================================================
#define MAX_ADMIN_ACCOUNTS 10         // Admin/staff accounts tracked in RAM

// ============================================================================
// ACTIVITY LOG
// ============================================================================
#define MAX_ACTIVITY_LOG_ENTRIES 300  // Rolling log - oldest entries drop when full

// ============================================================================
// SALES INVENTORY
// ============================================================================
#define MAX_SALES_HISTORY_DAYS 90     // Rolling daily summaries - oldest day drops when full

// ============================================================================
// TIME SYNC (needed for Night Promo hour checks)
// ============================================================================
#define TIMEZONE_OFFSET_SEC (8 * 3600)   // Philippines, UTC+8
#define NTP_SERVER          "pool.ntp.org"
#define NTP_SYNC_TIMEOUT_MS 15000
#define EXTEND_PROMPT_MS  600000      // Auto-extend prompt at 10 min

// ============================================================================
// LOGIN BRUTE-FORCE PROTECTION
// ============================================================================
// Applies to both /api/login (voucher/subscriber) and admin auth
// (requireAdmin()) - see GUIHandler's isLockedOut()/recordLoginFailure().
// Tracked per source IP in a small fixed-size table (RAM-bounded for the
// ESP8266); the least-recently-failed entry is evicted when full.
#define LOGIN_MAX_FAILURES        8       // failed attempts before lockout
#define LOGIN_LOCKOUT_MS          60000   // 1 minute lockout once the limit is hit
#define LOGIN_ATTEMPT_TABLE_SIZE  16      // distinct source IPs tracked at once

// ============================================================================
// MODES & OPERATION
// ============================================================================
#define MODE_HOTSPOT      0
#define MODE_PPPoE        1
#define MODE_DUAL         2

// Default operation mode (0=Hotspot, 1=PPPoE, 2=Dual)
#define DEFAULT_MODE      0

// ============================================================================
// ADMIN SETTINGS
// ============================================================================
#define ADMIN_PASSWORD_HASH "21232f297a57a5a743894a0e4a801fc3"  // MD5("admin")
#define API_KEY             "38vz2rb6nk"                          // API key for MikroTik scripts
// Telegram bot token/chat ID are admin-editable via Settings now (see
// AdminAPI::telegramBotToken/telegramChatId) - these constants are only
// the fallback default for a device that's never had them set there.
#define TELEGRAM_BOT_TOKEN  ""                                     // Optional: your Telegram bot token
#define TELEGRAM_CHAT_ID    ""                                     // Optional: chat ID for notifications

// ============================================================================
// TELEGRAM NOTIFICATIONS (see firmware/telegram.h)
// ============================================================================
// Sends happen from the main loop, never inline in a request handler -
// the HTTPS/TLS handshake to api.telegram.org can take 1-3+ seconds,
// which would otherwise delay the customer's own login response. Queued
// messages that can't be sent (WiFi down, Telegram unreachable) are
// dropped rather than retried indefinitely - a missed sales notification
// isn't worth risking an unbounded backlog on an 80KB-RAM device.
#define TELEGRAM_QUEUE_SIZE   8      // pending messages held at once (oldest dropped if full)
#define TELEGRAM_MAX_MSG_LEN  200    // characters per message

// ============================================================================
// HARDWARE PIN ASSIGNMENTS
// ============================================================================
#if defined(ESP32)
// ESP32 DevKit (ESP32-WROOM-32)
#define PIN_COINSLOT      14    // G14 — Coin slot pulse sensor (default; Settings > Coin Slot)
#define PIN_LED_STATUS    2     // GPIO2 — Status LED (built-in, lit on HIGH)
#define PIN_LED_ADMIN     26    // G26 — Admin indicator LED
#define PIN_RELAY         13    // G13 — Relay (acceptor power)
#define PIN_BUZZER        25    // GPIO25 — Buzzer
#define PIN_SETUP_BUTTON  0     // GPIO0 — the BOOT button; press it while the LED blinks fast after boot to re-enter Setup Mode
#define DEFAULT_COIN_PIN  "G14"
#define DEFAULT_RELAY_PIN "G13"
#define ADMIN_LED_LABEL   "G26"
#else
// NodeMCU (ESP8266)
#define PIN_COINSLOT      D5    // GPIO14 — Coin slot pulse sensor
#define PIN_LED_STATUS    D4    // GPIO2 — Status LED (built-in)
#define PIN_LED_ADMIN     D6    // GPIO12 — Admin indicator LED
#define PIN_RELAY         D7    // GPIO13 — Relay (for external coin lock)
#define PIN_BUZZER        D8    // GPIO15 — Buzzer for warnings
#define PIN_SETUP_BUTTON  D3    // GPIO0 — the onboard FLASH button most NodeMCU boards already have; press it while the LED blinks fast after boot to re-enter Setup Mode
#define DEFAULT_COIN_PIN  "D5"
#define DEFAULT_RELAY_PIN "D7"
#define ADMIN_LED_LABEL   "D6"
#endif
#define SETUP_BUTTON_WINDOW_MS 3000 // how long after boot that FLASH press is accepted

// ============================================================================
// FIRST-BOOT SETUP WIZARD (see firmware/setup_mode.h)
// ============================================================================
#define SETUP_AP_SSID     "ZxheiFi-Setup"  // WiFi network the device broadcasts while unconfigured
#define NODEMCU_MDNS_NAME "zxheifi-nodemcu" // zxheifi-nodemcu.local - kept for the PC/admin side only. Customer phones use nodemcu.zxheifi.lan
                                            // (a static DNS entry on the router), since iOS/Android captive-portal browsers don't resolve .local

// ============================================================================
// FILE SYSTEM
// ============================================================================
#define SESSIONS_FILE     "/sessions.json"
#define VOUCHERS_FILE     "/vouchers.json"
#define CONFIG_FILE      "/config.json"
#define CREDITS_FILE     "/credits.dat"
#define SUBSCRIBERS_FILE  "/subscribers.json"
#define ADMIN_ACCOUNTS_FILE "/admins.json"
#define ACTIVITY_LOG_FILE   "/activity.json"
#define SALES_HISTORY_FILE  "/sales.json"
#define TODAY_SALES_FILE    "/today.json"   // today's running totals - survive a reboot
#define NETWORK_CONFIG_FILE "/network.json"  // Setup Wizard's saved WiFi/MikroTik/initial-admin credentials
#define VENDOS_FILE         "/vendos.json"       // Main (id 0) + paired sub vendos - see vendo_registry.h
#define COLLECTIONS_FILE    "/collections.json"  // coin-box collection history

// ============================================================================
// SUB VENDOS (v2) - see vendo_registry.h / vendo_api.h / subvendo/
// ============================================================================
// An ESP8266 main unit has ~27KB of free heap at runtime, so it serves at
// most 3 sub vendos; an ESP32 main (part 4 of v2) serves 10.
#if defined(ESP32)
#define MAX_SUB_VENDOS  10
#define BOARD_NAME      "esp32"
#else
#define MAX_SUB_VENDOS  3
#define BOARD_NAME      "esp8266"
#endif
#define MAX_COLLECTIONS 50   // coin-box collections kept (oldest dropped)

// ============================================================================
// SIZE CONSTRAINTS
// ============================================================================
// Firmware must stay under 900KB - the real ceiling is the "4M (3M
// SPIFFS)" board layout's ~1,044,464-byte app partition (PlatformIO's
// own reported max), not an arbitrary round number; 900KB keeps a
// deliberate safety margin below that. Raised from the original 500KB
// budget on 2026-09-04 when Telegram notifications' BearSSL/TLS stack
// (ESP8266HTTPClient + WiFiClientSecureBearSSL) pushed a clean build to
// ~533KB - still less than 55% of the actual partition, just past the
// old round-number budget.
// GUI must stay under 100KB total
// Total NodeMCU flash usage: <900KB (see above)

#endif // CONFIG_H