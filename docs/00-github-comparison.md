# 📊 JUANFI ENHANCED — GITHUB COMPARISON

**Rewritten 2026-09-04** — the original version of this doc was written
without actually reading the source of
[github.com/ivanalayan15/JuanFi](https://github.com/ivanalayan15/JuanFi)
and got several claims wrong (most notably claiming Telegram
notifications were "preserved" when this project never implemented
them at all). This version is based on the real repo's file tree,
README, and on-login MikroTik script — see
[10-feature-roadmap.md](10-feature-roadmap.md)'s 2026-09-04 audit entry
for the full investigation.

## Original JuanFi — actual architecture
- **Firmware**: single `JuanFi-nodemcu.ino`, admin pages
  (`system-config.html`, `voucher-generate.html`) served **by the
  NodeMCU itself** via SPIFFS (`data/admin/`) — different from this
  project, where all GUI pages are served by MikroTik.
- **MikroTik side does real work**: the on-login hotspot script (RouterOS
  script language, runs on the router) creates a `/system scheduler`
  entry as the session's expiry timer, tracks daily/monthly sales by
  storing numbers in `/system script` source fields (a genuine hack —
  RouterOS scripts have a persistent `source` string property, reused
  here as ad-hoc storage), and sends Telegram notifications **directly
  from the router** via `/tool fetch` to the Bot API — no NodeMCU
  involvement in any of that. This project instead centralizes
  everything on the NodeMCU via the RouterOS *API* (binary protocol,
  port 8728) — a deliberate architectural choice, not an oversight, but
  it means anything the original did purely in router-script had to be
  reimplemented NodeMCU-side to carry over, and some of it wasn't.
- **Hardware options**: ESP8266 *and* ESP32, each with both a Wireless
  (NodeMCU joins the MikroTik's WiFi, what this project does) and a
  **LAN-based** variant (Ethernet via a W5500 SPI module — more
  reliable than WiFi for a payment kiosk, since it can't drop the WiFi
  link). This project is ESP8266-Wireless only.
- **Companion Android app** ("JuanFi Manager", on Google Play) talking
  to the author's own cloud relay (`juanfiapi.projectdorsu.com`) for
  remote monitoring — the reference architecture for this project's
  still-unstarted Phase E (remote/cloud management).

## Feature-by-feature status

✅ = genuinely covered (often differently) | ⚠️ = partially covered / different in a way worth knowing | ❌ = real gap, not implemented at all | ➖ = deliberately different by design, not a gap

| Original feature | Status | Notes |
|---|---|---|
| Coinslot → MikroTik integration | ✅ | Proportional-pulse coin credit per denomination (Settings → Rate Profiles), slot reserved per customer, configurable pins/relay (Settings → Coin Slot) |
| Voucher generation | ✅ | On-device + `vouchers/generator.py` for printed batches with QR |
| Sales tracking / dashboard | ✅ | 90-day rolling history split by payment method (coin/voucher/subscription) |
| Anti-coinslot-abuse system | ⚠️ | Covered by `MIN_PULSE_INTERVAL_US`/`MAX_PLAUSIBLE_PULSES`, but these are **compile-time constants**, not admin-configurable like the original's "coinslot abuse system config" — worth making Settings-editable |
| **First-boot setup wizard** (no-reflash WiFi/MikroTik config) | ✅ | Added 2026-09-04, see [12-setup-wizard.md](12-setup-wizard.md) — was a real, significant gap before this |
| **Telegram notifications** | ✅ | Added 2026-09-04 — NodeMCU-side HTTPS to the Bot API (`firmware/telegram.h`), since this project's architecture doesn't use RouterOS on-login scripts the way the original sends from the router. Admin-editable via Settings, unlike `config.h`-only in the original. |
| **"Night Light" physical relay control** | ❌ | Different from this project's "Night Promo" (a *pricing* discount during 12AM-6AM) — the original's night light toggles an actual light bulb via relay (`/admin/api/toggerNightLight`). Not implemented (the relay pin now powers the coin acceptor). |
| **Remote restart via HTTP** | ❌ | Original: `/admin/api/restartSystem` with an `X-TOKEN` header. Not implemented. |
| **Random MAC sync fix** | ✅ | Added 2026-09-04 — confirmed this project WAS affected (a MAC-changed reconnect would hit `redeemVoucher()`'s single-use rejection, and a stale `/ip/hotspot/active` entry would block the new MAC under `shared-users=1`). Fixed with `MikrotikAPI::kickActiveHotspotUser()` + a reconnect branch in `handleLogin()` — see [07-features-guide.md](07-features-guide.md)'s "Random MAC Reconnect Handling". |
| **QR code generation** (customer-facing, in-browser) | ✅ | Added 2026-09-04 — vendored the exact same `qrcode.min.js` (davidshimjs/qrcodejs, MIT) the original ships, fetched directly from its repo and scanned for anything suspicious before use. Verified via independent round-trip decode (jsQR), not just visual inspection. |
| QR code *scanning* (voucher entry) | ✅ | Browser-native `BarcodeDetector`, no vendored library needed |
| **LAN-based (Ethernet/W5500) hardware option** | ❌ | WiFi-only in this project |
| **Multi-vendo** (multiple coin-slot units per router) | ❌ | One NodeMCU per deployment in v1.0.0 — planned as Sub Vendo in [13-v2-masterplan.md](13-v2-masterplan.md) |
| **LCD on-device voucher generation** | ❌ | Hardware-dependent (needs an LCD); not implemented, would need explicit hardware wiring decisions if wanted |
| ESP32 support | ❌ | ESP8266-only |
| PPPoE mode | ➖ | Not in the original at all — this project's own addition |
| Modern "Coin Meter" theme GUI, mobile-responsive | ➖ | Original's own dark blue-gray + accent-stripe + big-tabular-numbers vocabulary (5 template versions, Bootstrap-based, v2.4 through 4.3, with their own sound effects/animations) was the actual grounding inspiration for this project's 2026-09 redesign — not a gap either direction, the original directly informed the result |
| Multi-admin roles, activity log, subscriptions, Gaming QoS, content filtering, Idle Auto-Pause, salted password hashing, login rate-limiting | ➖ | This project's own additions, not present in the original at all |

## Practical takeaway
The setup wizard (the most significant real gap), Telegram
notifications, real QR generation, and the random-MAC reconnect issue
are now closed (all 2026-09-04). The remaining ❌ items — Night Light
relay, remote restart, LAN/ESP32 hardware options, multi-vendo, LCD
on-device generation — are tracked in
[10-feature-roadmap.md](10-feature-roadmap.md) as scoped-but-not-yet-built,
to be prioritized with the user rather than assumed.
