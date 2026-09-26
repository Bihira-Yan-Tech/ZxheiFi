/*
 * pppoe.h - Dual mode (Hotspot / PPPoE) provisioning
 * ZXHEIFI - NodeMCU ESP8266 Firmware
 *
 * Wraps MikrotikAPI so the rest of the firmware doesn't need to know
 * whether a voucher is being redeemed for Hotspot or PPPoE mode — it
 * just calls provision()/deprovision() with the mode string that came
 * from the GUI's mode toggle.
 */
#ifndef PPPOE_H
#define PPPOE_H

#include <Arduino.h>
#include "config.h"
#include "mikrotik_api.h"
#include "qos.h"

class PPPoEManager {
public:
  void begin() {
    // Nothing to initialize; MikrotikAPI connects lazily on first use.
  }

  // Creates the MikroTik-side account for a redeemed voucher or logged-in
  // subscriber. `password` is what actually gets set as the account's
  // MikroTik password - pass a subscriber's real password here so their
  // PPPoE dialer / the hotspot's own login can authenticate with it;
  // leave it blank for vouchers, where the code doubles as both
  // username and password (there's no separate voucher password).
  bool provision(MikrotikAPI& api, QoSManager& qos, const String& sessionId,
                 const String& mode, const String& tier,
                 uint32_t limitUptimeSec, uint64_t limitBytesTotal,
                 const String& password = "") {
    String actualPassword = password.length() ? password : sessionId;
    String profile = qos.profileFor(tier, mode);
    if (mode == "pppoe") {
      return api.addPppSecret(sessionId, actualPassword, profile);
      // Note: RouterOS PPP secrets don't take a per-secret byte/time cap
      // the way hotspot users do — total usage is enforced by the
      // firmware's SessionManager, which calls deprovision() once the
      // budget in session.h hits zero.
    }
    return api.addHotspotUser(sessionId, actualPassword, profile, limitUptimeSec, limitBytesTotal);
  }

  bool deprovision(MikrotikAPI& api, const String& sessionId, const String& mode) {
    if (mode == "pppoe") return api.removePppSecret(sessionId);
    return api.removeHotspotUser(sessionId);
  }

  // Pulls the current bytes-in+out counter for a session so
  // SessionManager can deduct the delta since the last poll.
  bool pollTraffic(MikrotikAPI& api, const String& sessionId, const String& mode, uint64_t& totalBytes) {
    uint64_t in = 0, out = 0;
    if (!api.getActiveTraffic(sessionId, mode == "pppoe", in, out)) return false;
    totalBytes = in + out;
    return true;
  }
};

#endif // PPPOE_H
