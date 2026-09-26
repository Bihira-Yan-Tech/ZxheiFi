/*
 * qos.h - Tier-to-profile mapping and Night Promo scheduling
 * ZXHEIFI - NodeMCU ESP8266 Firmware
 *
 * The actual queueing/prioritization (fq_codel, gaming UDP marking) lives
 * on the router — see mikrotik/gaming_qos_fq_codel.rsc. This class only
 * decides *which* MikroTik user profile a voucher tier should get, and
 * whether Night Promo pricing/data currently applies. Profile names must
 * match the ones created by the mikrotik/ *_full_config.rsc scripts.
 */
#ifndef QOS_H
#define QOS_H

#include <Arduino.h>
#include <time.h>
#include "config.h"

class QoSManager {
public:
  void begin() {
    // Kick off NTP sync; Night Promo needs a real wall clock. If this
    // never succeeds (no internet uplink), isNightPromoActive() below
    // safely reports false instead of guessing.
    configTime(TIMEZONE_OFFSET_SEC, 0, NTP_SERVER);
  }

  // A real date, from NTP or from an admin's browser (see
  // GUIHandler::requireAdmin). The old check ("past 1970-01-01 02:00")
  // turned true after 16h of uptime even with no clock source at all.
  bool timeIsSynced() {
    return (uint32_t)time(nullptr) > 1700000000UL;
  }

  bool isNightPromoActive() {
    if (!timeIsSynced()) return false;
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    int h = t.tm_hour;
    if (NIGHT_PROMO_START_HOUR <= NIGHT_PROMO_END_HOUR) {
      return h >= NIGHT_PROMO_START_HOUR && h < NIGHT_PROMO_END_HOUR;
    }
    // Wrap-around window (e.g. 22 -> 6)
    return h >= NIGHT_PROMO_START_HOUR || h < NIGHT_PROMO_END_HOUR;
  }

  // Speed profile id -> MikroTik profile. "1"/"2"/"3" are the three the
  // desktop app's Config creates; any other id is one an admin added in
  // Settings > Speed Profiles, created on the router as zx-speed-<id> /
  // zx-pppoe-<id> when Settings is saved.
  static String hotspotProfile(const String& tier) {
    if (tier == "2") return "hs-gaming";
    if (tier == "3") return "hs-high";
    if (tier == "1" || !tier.length() || tier == "night") return "hs-default";
    return "zx-speed-" + tier;
  }

  static String pppoeProfile(const String& tier) {
    if (tier == "2") return "pppoe-gaming";
    if (tier == "3") return "pppoe-high";
    if (tier == "1" || !tier.length() || tier == "night") return "pppoe-default";
    return "zx-pppoe-" + tier;
  }

  String profileFor(const String& tier, const String& mode) {
    return (mode == "pppoe") ? pppoeProfile(tier) : hotspotProfile(tier);
  }
};

#endif // QOS_H
