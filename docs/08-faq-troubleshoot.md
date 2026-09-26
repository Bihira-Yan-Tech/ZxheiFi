# ❓ FAQ & Troubleshooting

## "Compilation failed" on the firmware
- Make sure you're including the whole `firmware/` folder as the
  sketch (Arduino IDE) or `src/` (PlatformIO) — `nodemcu_firmware.ino`
  `#include`s `mikrotik_api.h`, `session.h`, `qos.h`, `pppoe.h`,
  `admin_api.h`, `gui_handler.h`, `coin_slot.h`, `setup_mode.h` and the
  rest of the `.h` files, all of which must sit next to it. You normally
  don't need to compile at all: `firmware/zxheifi_firmware.bin` is the
  ready-built v1.0.0 image the desktop app flashes.
- Install ArduinoJson **v6.x** specifically (v7 changed the API).
- This codebase was verified to compile with PlatformIO
  (`espressif8266` platform, `nodemcuv2` board, ArduinoJson ^6.21.0) —
  about 560 KB flash, 0 errors (a handful of harmless SPIFFS-deprecation
  warnings are expected and safe to ignore), so if you see errors the
  most likely cause is a missing file or wrong ArduinoJson version.
  One version-specific gotcha already hit once: `ESP8266WebServer::
  collectHeaders()` is a variadic template on newer core versions (each
  header name its own argument) rather than the older `(array, count)`
  form — if you see a `collectHeaders` conversion error, that's why.

## Login/status/admin dashboard doesn't respond at all (no errors, just nothing happens)
The GUI is served by MikroTik but the API it calls lives on the
NodeMCU — a different device — so `script.js` has to know how to reach
it (see docs/07-features-guide.md's "Cross-Origin GUI ↔ NodeMCU
Communication" section for the full explanation of why). Check, in
order:
1. Open the browser's dev tools console/network tab while on
   login.html/status.html/admin.html — a failed cross-origin request,
   a CORS error, or a DNS/name-resolution failure here confirms this is
   the issue, not something else.
2. `mikrotik/gui/script.js`'s `NODEMCU_HOST` (default
   `nodemcu.zxheifi.lan`) must match the router's DNS entry:
   `/ip dns static print` should list it with the NodeMCU's address.
3. The NodeMCU really got that address: `/ip dhcp-server lease print`
   should show its MAC on the reserved IP (wrong MAC in Configure
   MikroTik = it gets a random address instead).
4. Not-logged-in phones can reach it: `/ip hotspot walled-garden ip
   print` should have an accept entry for the NodeMCU address.

## `/api/health` returns `"mikrotik": false`
The NodeMCU can't log in to the RouterOS API. Check, in order:
1. NodeMCU actually joined the WiFi (`WiFi connected, IP: 10.0.0.254`
   in the desktop app's Serial Monitor) — if not, the WiFi name/password
   entered in the NodeMCU's Setup Wizard don't match the router. For an
   open WiFi leave the password blank (firmware 1.0.0 also ignores a
   stale saved password when the network turns out to be open).
2. `/user print` on the router shows a `zxheifi-api` user, and its
   password is the "NodeMCU API pass" shown by the desktop app — the
   same one must be entered in the Setup Wizard. If unsure, run the
   wizard again (FLASH button while the blue LED blinks fast after
   power-on).
3. `/ip firewall filter print` allows port 8728 from the LAN — the
   provided configs already open it, but a custom firewall change can
   re-close it.

## Voucher says "invalid or used"
- Codes are single-use — `AdminAPI::redeemVoucher()` marks them used
  on first successful login and rejects the code afterward.
- Codes must be imported first: on-device `generateVouchers()` codes
  work immediately, but printed codes from `vouchers/generator.py`
  need to be imported via the admin dashboard's "Import Printed Batch"
  (CSV upload) before anyone can redeem them.

## Login succeeds but the user has no internet
- Hotspot mode: check `/ip hotspot active print` on the router — if
  the user isn't listed, `MikrotikAPI::addHotspotUser()` failed
  silently on the router side; check RouterOS logs (`/log print`) for
  the add attempt.
- PPPoE mode: check `/ppp secret print` and `/ppp active print` — the
  customer's PPPoE client must be configured to actually dial in;
  redeeming a voucher only creates the *credentials*, it doesn't
  connect anything on the user's device for PPPoE mode.

## Admin dashboard always asks for the username/password again
`script.js` caches the admin username+password in `sessionStorage`,
which clears when the browser tab closes — this is intentional (avoids
persisting the password indefinitely on a public-facing kiosk browser).

## Staff admin login gets 403 on Settings/Subscriptions/Admins
Expected — those three are super-only (`GUIHandler::requireAdmin(true)`
in `gui_handler.h`). The GUI hides those tabs for a staff login, but if
you're calling the API directly you'll see the 403. Log in as a super
account (the default first-boot account is `admin`, see `config.h`'s
`ADMIN_PASSWORD_HASH`) or ask a super admin to promote/create one via
the Admins tab.

## Can't disable/demote an admin account
If it's the *last active* super admin, `AdminAPI::setAdminAccountActive()`
refuses on purpose — otherwise a shop could lock itself out of its own
Settings and Subscriptions tabs entirely. Create or promote a second
super account first, then disable the one you actually want to remove.

## "Show QR" or the voucher print sheet shows no QR image
`qrcode.min.js` is missing from the router's hotspot folder — run
**Upload GUI Files** again (it verifies every file). The QR library is
vendored in `mikrotik/gui/` and works offline.

## Coin slot isn't registering coins
See [11-coin-acceptor-wiring.md](11-coin-acceptor-wiring.md) for the
full wiring/configuration/testing guide. Quick checklist:
- Confirm the acceptor's pulse output is wired to the pin chosen in
  Admin → Settings → Coin Slot (D5 by default) and shares ground with
  the NodeMCU.
- "Pesos per pulse" in the same section must match how your acceptor is
  programmed (1 pulse = ₱1 is the default; some acceptors pulse per ₱5).
- Every coin value needs a Rate Profile with that exact price — a
  `coin_unmatched` log entry means one is missing.
- The relay must actually power the acceptor during Insert Coin: check
  Relay pin and relay HIGH/LOW (the module's H/L jumper).
- Check the Activity Log (Admin dashboard → Logs tab) for a
  `coin_anomaly` entry — that means a burst got rejected as implausible
  (see `MAX_PLAUSIBLE_PULSES` in `config.h`), not that nothing was
  detected at all.

## Admin login stays on "Checking..." or says the password is wrong
- The admin password is the one entered in the NodeMCU's **Setup
  Wizard** (user `admin`) — not the MikroTik router's password.
- "Checking..." for 15 s then an error means the NodeMCU is offline.
  Open the desktop app's Serial Monitor: it should say `WiFi connected,
  IP: 10.0.0.254` and `SYSTEM READY`.
- Too many wrong tries lock logins for a minute (brute-force protection).

## A red "firmware outdated" banner on the Admin Dashboard
The portal pages are newer than the NodeMCU firmware. Flash the bundled
`zxheifi_firmware.bin` from the desktop app (settings, vouchers and sales
are kept), then reload the page. Until then Save Settings is refused on
purpose, because the old firmware doesn't know the new fields.

## Buttons do nothing / Save gives no message after an update
The phone's browser is still using an older cached `script.js`. The
pages detect this and reload themselves once; if the notice "please
refresh / clear cache" stays, close the browser completely (or clear
its cache) and open the page again.

## Save Settings says `not_saved_low_memory`
The NodeMCU refused to write because it was short on memory (it never
writes a half-finished config). Restart the NodeMCU and save again; if
it repeats, use fewer rate/speed profiles or a shorter announcement.

## The phone shows MikroTik WebFig or "404 Not Found" instead of the login page
- WebFig: the hotspot isn't running. On the router, `/ip hotspot print`;
  an `I` flag with "not allowed by device-mode" means run
  `/system device-mode update hotspot=yes scheduler=yes`, then unplug
  the router's power within 5 minutes.
- 404: the GUI isn't in the folder the hotspot uses (`hotspot` on a hAP
  lite, `flash/hotspot` on a hEX). Use **Upload GUI Files**.

## "Invalid username or password" / Winbox keeps logging out during setup
Almost always the PC reached a *different* MikroTik: every MikroTik is
192.168.88.1 by default, and a flapping Ethernet link (network-card power
saving) makes Windows fall back to WiFi and your main router. Run
**Network Check** in the desktop app — it shows the route, link drops and
every MikroTik it hears, and prints the commands to switch off Energy
Efficient / Green Ethernet. Then manage the unit at its own address,
10.0.20.1 (PC on the `lan` port) or 10.0.0.1 (hotspot port), or by MAC in
Winbox.

## The Flash tab stays on "Scanning..."
A Bluetooth COM port is probably selected — pick the `USB-SERIAL`
(CH340/CP210x) one; USB ports are listed first. Cancel Scan stops it.
Scanning is optional: the MAC is also read during Flash.
