# ⚙️ Flashing the NodeMCU

Full build/flash steps live in
[`firmware/BUILD_INSTRUCTIONS.md`](../firmware/BUILD_INSTRUCTIONS.md) —
this page is the short version plus the ZxheiFi-specific config.

## 1. Configure before compiling (mostly optional now)
As of the [Setup Wizard](12-setup-wizard.md) (2026-09-04), WiFi/MikroTik
credentials and the admin dashboard password **no longer need to be
edited in `config.h` before compiling** — the wizard collects them from
a phone/laptop on first boot instead, no reflashing needed per
deployment. The same `config.h` constants (`WIFI_SSID`, `WIFI_PASSWORD`,
`MIKROTIK_API_USER`, `MIKROTIK_API_PASS`, `ADMIN_PASSWORD_HASH`) remain
as fallback defaults for a device that's never run the wizard (dev/
testing builds).

Still worth editing before compiling if your setup differs from the
defaults: tier pricing/time/data limits (also editable later via
Settings → Voucher Pricing, but these are the *starting* values),
Night Promo hours, GPIO pin assignments if your wiring differs.

## 2. Build
Either Arduino IDE (`firmware/BUILD_INSTRUCTIONS.md`) or PlatformIO:
```
cd firmware
python -m platformio run --environment nodemcuv2   # if you set up a platformio.ini
```
Confirm the build log's "Flash:" line stays under 900,000 bytes (see
`config.h`'s SIZE CONSTRAINTS section — raised from an original 500KB
round-number budget to reflect the actual ~1,044,464-byte "4M (3M
SPIFFS)" app partition ceiling) — the reference build for this codebase
(as of the 2026-09-04 Telegram notifications addition) came in at
532,759 bytes (~51%), 37,052 bytes RAM. `ESP8266HTTPClient`/
`WiFiClientSecureBearSSL` (the BearSSL/TLS stack Telegram needs) is by
far the largest single flash addition this project has made in one
change (~100KB) — still bundled with the `esp8266` platform, not a new
external dependency, but worth knowing about if you're tight on flash
and don't need Telegram (there's no build flag to exclude it yet).

**Confirm `mikrotik/gui/script.js`'s `NODEMCU_HOST` matches
`config.h`'s `NODEMCU_MDNS_NAME`** (plus `.local`) — the GUI is served
by the router, not the NodeMCU, so if the two ever drift apart the
dashboard silently can't reach the API in production (see
[12-setup-wizard.md](12-setup-wizard.md) and docs/07-features-guide.md's
"Cross-Origin GUI ↔ NodeMCU Communication" section).

## 3. Flash
NodeMCU-PyFlasher, esptool, or Arduino IDE's Upload button, at
115200 baud, `dio` flash mode, full chip erase recommended.

## 4. First boot checklist
Watch the serial monitor (115200 baud):
1. `SPIFFS Mount Failed` on first boot is normal — it auto-formats.
2. `=== SETUP MODE ===` — expected on a genuinely fresh device (see
   [12-setup-wizard.md](12-setup-wizard.md)). Connect to the
   `ZxheiFi-Setup` WiFi network from a phone/laptop, fill in the form,
   submit — the device reboots into normal operation automatically.
   If you'd rather configure via `config.h` instead (dev/testing), skip
   the wizard form and it'll keep using the compile-time
   `WIFI_SSID`/`WIFI_PASSWORD`/etc. constants as before.
3. `WiFi connected, IP: ...` — if this times out, double check the
   WiFi credentials (wizard-entered or `config.h`, whichever applies)
   against the router.
4. `mDNS responder started: zxheifi-nodemcu.local` — the PC-side name.
   Customer phones use `nodemcu.zxheifi.lan` instead (a DNS entry the
   desktop app puts on the router).
5. `=== SYSTEM READY ===` — HTTP API is now listening on port 80.
6. Hit `http://nodemcu.zxheifi.lan/api/health` (or the NodeMCU's IP
   directly) from a device on the same network — `{"mikrotik":true,...}`
   confirms the RouterOS API login succeeded.

If `mikrotik` stays `false`, check the MikroTik API username/password
(wizard-entered or `config.h`'s `MIKROTIK_API_USER`/`PASS`) and that
`/user print` on the router shows the `zxheifi-api` account with the
`api-read` group.
