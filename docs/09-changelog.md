# 📝 Changelog

## v2.0.0-dev - 2026-10-02 - ESP32 main unit + Backup/Restore (part 3 of v2)

Spec: `docs/superpowers/specs/2026-10-02-esp32-main-backup-restore-design.md`;
guide: [16-esp32-and-backup.md](16-esp32-and-backup.md).

- **ESP32 DevKit main unit:**
  - `firmware/platform.h` maps the board differences (web server, SPIFFS,
    HTTPS client, mDNS, random, open-WiFi constant, header collection, LED
    polarity, pin labels), so one source tree builds both boards
    (`main_esp8266` / `main_esp32`);
  - up to **10** coin boxes on an ESP32 (3 on a NodeMCU);
  - pins on the ESP32: coin G14, relay G13, buzzer GPIO25, LED GPIO2, BOOT =
    setup;
  - `tools/merge_esp32.py` makes `zxheifi_firmware_esp32.bin`, a single image
    flashed at 0x0 like the NodeMCU's;
  - build size: 1,165,221 of 1,310,720 bytes (default.csv, OTA-ready).
- **Backup/Restore** (Admin → Settings → Backup & Restore):
  - `GET /api/admin/backup` streams every data file into one JSON. That
    includes vouchers, subscribers, admins (salted hashes), sales, logs, live
    sessions and the coin boxes with their keys, but never `network.json`.
  - `POST /api/admin/restore` (multipart, at most 1 MB):
    - the upload is streamed to flash;
    - `backup_split.h` (pure C++, 18 host tests incl. every chunk size) splits
      it per file and validates the WHOLE backup before anything changes;
    - the swap happens in `loop()` right before the restart, so no periodic
      save can overwrite it.
  - The restore is logged after the reboot. Pins from another board are reset
    to the defaults and logged (`pins_reset`).
- **Admin:**
  - Coin Slot pin dropdowns come from the board's `pinChoices` (health and
    settings now report `board` + `pinChoices`);
  - a Backup & Restore section, with a confirm dialog that shows the backup's
    board/firmware/date and a privacy warning.
- **Setup Companion:**
  - Device type **Main unit (ESP32)**;
  - the flasher passes the chip (`esp8266` / `esp32`), so the wrong board
    stops with a clear message;
  - Scan Device uses `auto`;
  - EN/TL Guide section "ESP32 Main Unit and Backup".
- **Tests:**
  - host C++ (protocol, charge logic, backup splitter) all pass;
  - `vendo_test.py` 162/162 (adds backup/restore and pin choices);
  - regression 103/103.

## v2.0.0-dev - 2026-09-30 - Charging Station (part 2 of v2)

Spec: `docs/superpowers/specs/2026-09-30-charging-station-design.md`;
user guide: [15-charging-station.md](15-charging-station.md).

- **Charging Station firmware** (`charging/`): a coin-op phone charger box.
  - Hardware: 4 USB ports switched through a PCF8574 I2C expander, a button
    per port, an optional SSD1306 OLED screen, and the coin acceptor.
  - **The box runs the charging itself**, so it works with no WiFi or with
    the main unit down.
  - Every sale is queued in LittleFS (up to 100, then one combined overflow
    record) and sent over the signed protocol until acknowledged.
  - Port times survive power cuts (saved every 30 s and on change).
  - Admin Stops arrive in poll replies and are acknowledged.
  - Tagalog/English screens.
- **`charging/charge_logic.h`**: all charging rules in pure C++, with 35 host
  tests covering exact and fallback pricing, cap, top-up, window, held coins,
  stop, power-cut resume and `millis()` wrap. They run on the PC via
  `tools/run_host_tests.py`.
- **`common/box/`**: the Sub Vendo's storage, record queue, Setup Wizard and
  signed link moved here and generalized. Both boxes now share them, and the
  Sub Vendo's behaviour is unchanged.
- **Main unit:**
  - vendo `type` (`wifi` / `charging`) and `POST /api/vendo/charge`;
  - charging polls carry port times, and replies carry Stops;
  - `POST /api/admin/vendos/stop`;
  - Settings → **Charging** (rates, max per port, screen language), pushed to
    every box through `cfgVer`;
  - `chargingRevenue` in the daily sales, totals, Overview and CSV;
  - `wrong_type` guards both ways; charging boxes are hidden from the customer
    picker and count toward the box limit.
- **Portal:**
  - Add Vendo is now a form with **Type**;
  - charging rows show `P1 libre · P2 23m` with **Stop**;
  - Edit gets ports (1-4) and port relay HIGH/LOW, and blocks D1/D2 for the
    coin pins;
  - Settings → Charging section;
  - a Sales **Charging** column.
- **Setup Companion:** a third Device type, **Charging Station** (bundled
  `.bin`), and an EN/TL Guide section.
- **Tests:**
  - `tools/vendo_test.py`: 133 checks, with `ChargeSim` in
    `tools/sub_sim.py`, a per-test port, and a 40 s start-up allowance for a
    busy PC;
  - regression 103/103;
  - host C++ tests all pass.

## v2.0.0-dev - 2026-09-26 - Vendos framework + Sub Vendo (part 1 of v2)

Built on branch `v2` (folder `ZxheiFi-v2`); v1.0.0 on `main` is untouched.
Spec: `docs/superpowers/specs/2026-09-26-vendos-framework-subvendo-design.md`;
user guide: [14-sub-vendo.md](14-sub-vendo.md).

- **Sub Vendo firmware** (`subvendo/`):
  - pairs with a one-time code and polls the main unit (2 s idle, 1 s while a
    customer inserts);
  - relay ON only while reserved *and* the main unit answered within 3 s
    (fail-closed);
  - every coin is queued in LittleFS until the main unit signs an "ok";
  - its own Setup Wizard (`ZxheiFi-Sub-Setup`) and LED/buzzer states.
- **Signed protocol** (`common/zx_protocol.h`, shared by both firmwares; Python
  reference `tools/zx_protocol.py`):
  - HMAC-SHA256 on every request and reply;
  - keys derived from the pairing code, never transmitted;
  - message counters (replay), coin sequence numbers (dedupe) and reservation
    ids (a late coin reaches the customer who paid it, never the next one).
  - Pinned to RFC 4231 + project vectors on three sides:
    - Python;
    - PC-compiled C++ (`tools/run_host_tests.py`, PlatformIO MinGW);
    - on-device (`common/selftest`).
- **Main unit:**
  - `vendo_registry.h` (vendos, keys, box totals, collections);
  - `vendo_api.h` (`/api/vendo/*` + `/api/admin/vendos*`);
  - `coin_slot.h` now keeps one reservation per vendo, so two boxes can serve
    two customers at once;
  - sales keep a per-vendo split (`byVendo`, old days = all Main);
  - coins nobody claims are counted as sales and logged `coin_late_unclaimed`
    instead of vanishing;
  - Telegram offline/online alerts per box;
  - limit 3 sub vendos on an ESP8266 main unit (10 on ESP32, part 4).
- **Portal:**
  - "Coin box" picker on login/status (only when there is more than one box;
    `?vendo=` from the QR sticker; remembered per phone; offline boxes
    disabled);
  - Admin **Vendos** tab (Add Vendo + pairing code, status, today, coin box,
    commission, Collected, QR sticker, Edit pins, Re-pair, Remove,
    Collections);
  - Sales filter per box with host/owner split;
  - **Export per-vendo CSV**;
  - `vendo-sticker.html`.
- **Setup Companion:**
  - Flash tab **Device type** (Main unit / Sub Vendo), which bundles both
    `.bin` files; a sub's MAC is not copied into Configure MikroTik;
  - Guide section "Sub Vendo" in English and Tagalog.
- **Build and tests:**
  - in-repo PlatformIO projects (`pio run -d firmware|subvendo`);
  - `tools/vendo_test.py` (92 checks, fresh mock per test, test clock);
  - the test tools use `127.0.0.1`: `localhost` cost 2 s per request on
    Windows, so `regression_test.py` went from about 3 min to about 2 s.
- **Version:** `2.0.0-dev` everywhere. `versionAtLeast()` treats a `-dev`
  build as older than its release.

## v1.0.0 — 2026-09-26 — First release

The firmware, portal and Setup Companion now share one semantic version
(`FIRMWARE_VERSION`, `ZX_GUI_VERSION`/`REQUIRED_FIRMWARE`, `APP_VERSION`,
installer `AppVersion`). The old date stamps ("2026.09.26.2") are
treated as older than 1.0.0 by `versionAtLeast()` in `script.js`, so a
NodeMCU still on a pre-release build gets the red "firmware outdated"
banner until it's reflashed.

- **Sounds included.** A starter set of 7 original MP3s in
  `mikrotik/gui/sounds/` (about 380 KB): an insert-coin voice prompt
  (Windows TTS) plus synthesized coin drop, success, error, click, a
  16 s counting loop and a 23 s background loop. All decode in the
  browser; they're uploaded by Upload GUI Files. Still off until the
  admin ticks Enable Sounds.
- **Bilingual in-app Guide.** English/Tagalog switch at the top of the
  Guide tab (remembered in the settings file). Rewritten for v1.0.0:
  automatic GUI upload, Admin Dashboard walkthrough (Speed/Rate
  Profiles, vouchers print, sales CSV, coin pins, sounds), and a new
  "Problems we hit and how they were fixed" section with the real
  issues from hardware testing. `save_settings()` now merges, so the
  Configure MikroTik tab and the Guide never overwrite each other's keys.
- **Open-source preparation.**
  - `README.md` rewritten for v1.0.0.
  - `LICENSE` (MIT, © 2026 ZxheiFi).
  - New `THIRD-PARTY-NOTICES.md`: esptool GPL-2.0-or-later inside the
    exes, qrcodejs MIT, ESP8266 core LGPL-2.1, ArduinoJson MIT, and the
    rest.
  - `.gitignore`.
  - Credits: inspired by JuanFi by Ivan Julius Alayan (not affiliated).
- **Installer** shows the MIT license page and installs `LICENSE.txt` +
  `THIRD-PARTY-NOTICES.md`.
- **Docs:**
  - 06: automatic upload, no hardcoded prices, sounds included.
  - 07: Speed Profiles rewritten for named profiles, plus new sections
    for coin session flow, price list/announcement, voucher print sheet,
    sales CSV, open WiFi and version checks.
  - 08: stale config.h advice replaced; new FAQ entries for the admin
    login, outdated firmware, stale cache, `not_saved_low_memory`,
    WebFig/404, wrong-router login and stuck scans.
  - New [13-v2-masterplan.md](13-v2-masterplan.md) covers sub vendo,
    charging station, e-wallet, Fair Time, remote management and the
    ESP32 plan.
- **Final audit:**
  - Firmware 1.0.0 builds with zero warnings apart from the SPIFFS
    deprecations; a date buffer was widened to silence
    `-Wformat-truncation`. Build size: 554,879 B flash (53.1%), 38,956 B
    static RAM.
  - `test_configurator.py` ALL PASSED; `regression_test.py` 103/103;
    `node --check` on script.js and every inline page script.
  - `versionAtLeast()` unit cases pass.
  - Browser run on the mock server: price list, announcement, sounds
    decode, admin login, add Speed Profile + Save ("Na-save"), voucher
    generate → print sheet with 3 QR cards, Insert Coin ₱10 + ₱20 →
    3 h session on a stable status page, no console errors.

## 2026-09-26 (3) — NodeMCU offline after the WiFi went open
Admin login sat on "Checking...": the NodeMCU couldn't join ZXHEIFI - it
still had the old WPA2 password saved, and the ESP8266 refuses an open
network when given one. Firmware 2026.09.26.2 scans first and joins open
networks without the saved password (a secured or not-yet-visible network
still uses it, and a watchdog re-checks every 30 s, so a brownout where the
router boots slower can't strand it). Verified on the real NodeMCU: flashed
over COM5, "WiFi ZXHEIFI is open - connecting without the saved password",
IP 10.0.0.254, /api/health mikrotik=true, 27 KB free heap. Admin login now
times out after 15 s with a message instead of hanging. GUI 2026.09.26c.

## 2026-09-26 (2) — Audit after the first admin-panel test
Reported: Add Speed Profile did nothing, no speed dropdown, Save gave no
message, no voucher types. Reproduced both causes: (1) the phone kept an
older script.js in its cache while admin.html was new - buttons pointed
at functions that didn't exist and Save crashed silently; (2) new pages
against firmware that predates Speed Profiles. Fixes:
- /api/health reports the firmware version (FIRMWARE_VERSION); the admin
  page shows a red banner when it is older than REQUIRED_FIRMWARE and
  refuses to Save with a clear reason.
- Each page checks ZX_GUI_VERSION after loading script.js; on a mismatch it
  refetches script.js/style.css once (cache: reload) and reloads, then
  shows a "refresh / clear cache" notice if still stale (no loop).
- Save Settings: Saving... state, visible result line (saved / saved but
  MikroTik unreachable / exact error), 60 s timeout, page errors caught.
- Firmware: settings request document freed before saving; saveSettings
  refuses to write when out of memory (507) and writes via temp+rename, so
  a low-heap save can no longer wipe config.json; rate-profile import rolls
  back on failure; speed-profile push stops at the first connection failure.
- Vouchers: no rate profiles -> explicit message, Generate disabled; print
  tab opened with an absolute URL; empty speed list says so in dropdowns.
Tests: regression 103/103, desktop ALL PASSED, browser: stale-script,
old-firmware and normal flows with real clicks, full customer flow.

## 2026-09-26 — Admin panel features + fixes from the first live test
**Fixes found on the real hAP lite**
- Portal "Christmas lights": status.html sent a browser with no saved
  session (e.g. logged in via the phone's sign-in popup, opened in
  Chrome) to login.html, which MikroTik bounced straight back. status.html
  now takes the session from MikroTik's `$(username)`, shows a message +
  "Log out of the WiFi" instead of redirecting, and a loop breaker stops a
  3rd redirect in 15 s. Disconnect goes through MikroTik's /logout.
- Customer WiFi is OPEN by default (piso-WiFi style) - a blank WiFi
  Password in Configure MikroTik creates `zxheifi-open` (mode=none); a
  password still gives WPA2. Remove puts wlan1 back on "default" first.
- Upload GUI Files: retries, clearer phase logging, FTP always restored,
  size check waits for the hAP lite's slow flash (was a false "failed").
- Test Connection no longer flags RouterOS's own disabled "place hotspot
  rules here" rules as someone else's config. API timeout 6 s -> 15 s.
- Network Check (Configure MikroTik) + a "not a direct connection" guard:
  the WiFi -> Tenda -> hEX path to the same 192.168.88.1 caused the wrong
  password / Winbox drops (with a Realtek NIC power-saving link flap).
- Sales: today's totals were RAM-only (lost on every reboot) and the
  first date-known tick after boot zeroed them. Now saved to /today.json
  on every sale + periodically; the first tick only names the day. Data
  used per day is now actually counted.

**New admin panel features**
- Vouchers: Generate opens a printable sheet (print.html) in a new tab -
  QR code, code, amount, time, data, validity (with the date) per card,
  Print / Save as PDF; a link re-opens it if a pop-up blocker stops the tab.
- Sales: range filter (today / 7 days / this month / 30 days / all / a
  specific date / custom range), a Total column and summary, Export CSV.
- Speed Profiles: a named, editable list (up to 8) instead of 3 fixed
  Mbps rows. Rate Profiles and Subscribers pick a speed from a dropdown;
  new speeds are created on MikroTik as zx-speed-<id>/zx-pppoe-<id> on
  Save; a speed still in use can't be removed. Speed + rate profiles save
  in one request so a new speed can be used immediately.
- Portal: rate list (Amount / Time / Validity as 01d:00H:00M) at the
  bottom of login.html; Settings > Announcement shown on top of
  login/status. Rate rows show the d/h/m equivalent while editing.
- Settings > Coin Slot: coin pin, relay pin (or none), relay HIGH/LOW
  trigger, pesos per pulse - applied live, no reflash.
- Sounds: named slots - insert-coin (prompt), coin-drop (each coin),
  coin-music (loops during the coin countdown, background music pauses),
  success, error, click, background-music. sounds/README.md rewritten.
- Config.json load/save buffers 8192/6144 for the larger settings.

Tests: regression 103/103 (new: speed+rate atomic save, in-use guard,
pins, portal announcement/rates, subscriber speed/sales), desktop 49+
checks, browser click-through of every feature on the mock. Firmware
557,008 bytes. Not yet on hardware: speed profile push, coin pins, relay.

## 2026-09-25 — Full audit: bug fixes across app, firmware, GUI and router scripts
Triggered by a real incident: Configure MikroTik reached the user's
production hEX (through the PC's WiFi → Tenda → hEX path, while the
Ethernet sat on APIPA) with "hAP lite" selected, and its blanket-drop
firewall took the whole house offline. The audit covered the
desktop app, firmware, customer GUI and `.rsc` scripts.

**Desktop app (Configure MikroTik)**
- **Target check**: Test Connection now probes the router (identity,
  board, RouterOS version, ports, hotspot package, existing non-ZxheiFi
  config) and Config refuses a board that doesn't match the selected
  device, re-probing right before it writes anything. It asks first if
  the router already serves someone.
- **Idempotent plan** (`mikrotik_commands.py` rewritten,
  `configurator.py` new): every step is find → add, or update if
  ZxheiFi made it, or leave alone. Everything ZxheiFi creates is
  tagged, and the new **Remove ZxheiFi Config** button removes exactly
  that.
- Defconf-style firewall replaces "drop all other input/forward".
- NodeMCU API user gets **write** (was read-only, so every login failed
  to provision). The password is generated once and persisted, not
  `changeme123`.
- Walled-garden entry plus `nodemcu.zxheifi.lan` DNS so the login page
  can reach the NodeMCU. The hotspot pool is created before the server.
- Keeps the installer's session alive: the setup address moves onto the
  bridge before its port joins, the PC is bypassed from the hotspot, and
  the app reconnects on drops.
- Gaming QoS actually shapes (parent capped at the Bandwidth fields;
  fq-codel only on RouterOS 7). PPPoE clients get the router's DNS.
  Real port lists (hEX has no ether6).
- `test_configurator.py` (new): FakeRouter tests for a fresh router,
  re-run idempotency, removal, a foreign hEX on RouterOS 6, and a
  missing hotspot package.
- `export_rsc.py` (new): **all `mikrotik/*.rsc` are now generated from
  the app's plan**. The hand-written ones had the wrong model, hotspot/
  PPPoE on a bridged wlan1, the pool after the server, a blanket-drop
  firewall, a read-only API group, and undeclared globals.

**Firmware**
- `sendOk()` recursed into itself, which crashed the NodeMCU on
  disconnect/pause/resume and most admin actions.
- Session slots were never reused, so after 20 logins since boot new
  customers went untracked. Data-exhausted sessions were never
  deprovisioned.
- Vouchers, subscribers, logs and sales now persist through a
  per-record streaming JSON writer (temp file, then rename). Fixed-size
  documents lost everything past ~100 vouchers on reboot. List endpoints
  stream too.
- Vouchers made while the clock is unsynced get no expiry (they used to
  expire instantly once NTP synced). Subscriber add/renew waits for
  the clock. New codes are 8 characters (`ZX` + 6, no 0/O/1/I), and
  login matches them case-insensitively.
- Failed provisioning hands the voucher back (`rollbackVoucher`, which
  also reverses the sale count). A full session table undoes the
  provision instead of giving untracked free access. Extend provisions
  before it credits.
- RouterOS 7.18+ `!empty` replies no longer drop the API connection.
  Block is de-duplicated.
- Time is charged by real elapsed seconds, not one per loop tick.
- **Coin slot now belongs to a customer** (`coin_slot.h`, new): Insert
  Coin reserves the slot (the relay on D7 turns on), each coin looks up
  its own Rate Profile, and Done (or 45s idle) provisions a real code
  and logs the phone in. There is top-up from the status page, a coin
  dropped just before tapping is credited, and one customer at a time.
  Before this, coins created an anonymous `COIN-<millis>` user nobody
  could log in as.
- Setup Mode: FLASH is now read during a 3-second window after boot
  (LED blinks fast). "Hold during boot" could never work, since GPIO0
  low at power-on enters the ROM bootloader.
- Status LED polarity fixed (the D4 LED is active-low). The wizard's API
  password field is visible, with auto-capitalize off.
- 549,936 bytes (52.7% of the app partition).

**Customer GUI**
- `NODEMCU_HOST` is now `nodemcu.zxheifi.lan`, because phone
  captive-portal browsers don't resolve `.local`.
- Coin modal (live ₱/minutes, countdown, Done/Cancel) on login.html and
  "Add Time (Coins)" on status.html. MikroTik's `$(mac)` is passed along.
- `tools/mock_server.py` mirrors the coin API (plus `POST /dev/coin` to
  simulate a coin). `tools/regression_test.py` gained 20 coin/voucher
  checks: 93/93 pass. The browser click-through (insert → Done → status
  → top-up) was verified against the mock.

**Docs**: the in-app Guide was rewritten (router prep, PC static IP,
isolation warning, wizard values, coin slot, troubleshooting).
`mikrotik/import_guide.md` was rewritten, and docs 05/06/07/08/12 were
updated for the new hostname and FLASH window.

**Flash Firmware: "Erase everything first (fresh start)"** checkbox (off by default, confirm dialog, one-shot) runs esptool write-flash --erase-all, wiping saved setup/accounts/vouchers/logs so the NodeMCU boots into Setup Mode like a new unit. A normal flash only rewrites the firmware area.

**Flash tab scan fix**: the app sat on "Detecting..." because Bluetooth serial ports (COM3/4/6/7) were listed first and auto-probed (15-50s each, and a port change was ignored meanwhile). Now: USB ports first with descriptive labels (Bluetooth marked), no automatic scan, a Scan Device / Cancel Scan button, the MAC also read from the flash output, cancellable probes that kill the whole worker process tree, and --esptool-worker handled before the GUI imports (saved ~9s per esptool run installed, ~29s portable).

**Admin page fixes (from the first real-phone test)**: the admin page asked with two back-to-back prompt() boxes ("Admin username:" then "Admin password:") that read as the same question twice; typing the password into both 401'd, which wiped the saved login and re-prompted on every tab. Replaced with one login form verified against the NodeMCU before saving, clear wrong-password / lockout / unreachable messages, and a Logout button. Rate Profile rows missing a peso amount or minutes were silently dropped before saving (looked like Save did nothing) - now listed by row; duplicate peso amounts are rejected (firmware + page) and profiles are capped at 20. config.json load/save buffers raised (6144/4096) - at 3072 a larger profile or blocked-MAC list failed to parse on boot and silently reset every setting. Blocked MACs capped at 30. With no internet the NodeMCU never got NTP time, so Add Subscriber (which needs a real clock) could never save: a logged-in admin's browser now supplies the time via X-Client-Time until NTP takes over. QoSManager::timeIsSynced() no longer reports true after 16h of uptime with no clock. Hotspot folder is `hotspot` on RouterOS 7 (docs said flash/hotspot). Firmware 550,352 bytes.

**Ports / hotspot folder (user report)**: toggling Device to hEX after testing a hAP lite kept the hAP lite's probed ports; probed ports are now used only while the connected board matches the selected device. hAP lite (RB941-2nD) has 4 ethernet ports + wlan1 - the fallback list and the generated .rsc had a non-existent ether5. The hotspot folder depends on the router, not the RouterOS version: flash/hotspot when Files has a flash folder (hEX), hotspot when not (hAP lite) - the app already probes this; docs/Guide/hEX .rsc corrected.

**Device-mode check + Upload GUI Files** (both from the first real test): Test Connection now reads /system/device-mode; Config refuses when it forbids the hotspot (and asks when it forbids the scheduler with Daily Reboot/Random MAC Fix checked), giving the exact update command and the power-cycle step. New gui_upload.py + "Upload GUI Files" button: reads html-directory from the zxheifi-hs profile, runs /ip hotspot reset-html only when MikroTik's default pages are missing, uploads mikrotik/gui over FTP (enabling the ftp service temporarily if it was off, restoring it after), skips a stale local hotspot/ copy and README files, then verifies each file's size on the router. GUI bundled into both exes. Tests: test_configurator 43 checks incl. fake FTP; plus a run against a real local FTP server (pyftpdlib) - 7 files byte-identical, MikroTik's rlogin.html untouched.

**Not hardware-tested yet**: the new firmware on the real NodeMCU, the
coin flow with a real acceptor, and the generated `.rsc` on a real
router.

## 2026-09-22 (8) — In-app Guide tab, LICENSE, TERMS.md, README rewrite
User asked to try Configure MikroTik but flagged not having much
networking background, and separately asked for a full in-app guide
(readable inside the desktop app itself), plus a license, documentation,
and Terms and Conditions - explicitly to prepare the project for a
public GitHub repo. Confirmed with the user: **MIT license**, copyright
holder listed as "ZxheiFi" (brand name only, no personal name/email).

**New `desktop-app/guide_content.py` + `GuideTab`** (`desktop-app/
main.py`) - a third tab, "Guide", added to the app's `CTkTabview`
alongside Flash Firmware and Configure MikroTik. Static, scrollable,
styled to match the app's existing theme (accent-colored section
headers over `theme.PANEL` cards, same visual pattern already used by
`MikrotikTab`'s Port Roles panel). Content is Filipino/Taglish on
purpose (the actual target users - sari-sari store/laundry-shop
operators deploying a unit - are Filipino), covering: what the app is
for and the 3-step order of operations, a full field-by-field/checkbox-
by-checkbox walkthrough of Configure MikroTik (this is what the user
specifically said they didn't understand - explains every one of the 9
`FEATURES` checkboxes plus Port Roles, transcribed from
`mikrotik_commands.py`'s own descriptions so it can't drift out of sync
with what the buttons actually do), the manual Winbox GUI-upload step
(closes the exact confusion from earlier in this session), the optional
sound system, common troubleshooting, and an About/License section.
Window resized from 900x640 to 960x680 (`minsize` 760x480) to fit the
extra tab's content comfortably. Visually verified via screenshot -
renders correctly, matches theme.

**`LICENSE`** (new, root) - standard MIT license text, copyright
ZxheiFi.

**`TERMS.md`** (new, root) - plain-language Terms and Conditions
explicitly framed as *not* legal advice and secondary to the LICENSE
file: no-warranty/use-at-own-risk, operator responsibility for their
own regulatory/tax/business-registration compliance (using the
Philippines as a worked example, not a blanket legal claim), physical/
electrical safety for coin-acceptor wiring, no liability, fork/
attribution expectations, and a best-effort support disclaimer typical
of a volunteer open-source project.

**`README.md`** (full rewrite) - the previous version was from the
project's very first phase (predated the desktop app, admin dashboard,
sound system, Setup Wizard, and unified Rate Profiles entirely, and
still told readers to go clone the *original* JuanFi and manually merge
in changes - long obsolete). Rewritten to describe the current
architecture, an accurate feature list, the real repository layout
(including `desktop-app/`, which the old README never mentioned), a
Getting Started section pointing at the in-app Guide and the numbered
`docs/*.md` files, and License/Terms sections linking the two new files.

Rebuilt `desktop-app/dist/*.exe` (both portable and installer) to
include the new Guide tab. Did not commit anything to git - the repo
(confirmed to exist, `main` branch, one prior "Initial commit" with
everything since left untracked) is left for the user to stage/commit
themselves when ready to push.

## 2026-09-22 (7) — Firmware recompiled (soundEnabled + earlier changes)
`zxheifi_firmware.bin` was stale - the version on disk (540,784 bytes,
built 16:22) predated the `AdminAPI::soundEnabled` field added to
`firmware/admin_api.h`/`firmware/gui_handler.h` at 16:58 (see entry (3)
above). Recompiled via a scratch PlatformIO project (`platformio.ini`
with `env:nodemcuv2`, `espressif8266` platform, `ArduinoJson` lib_dep -
same config `BUILD_INSTRUCTIONS.md` documents as the PlatformIO
alternative to Arduino IDE), since no `platformio.ini` is checked into
`firmware/` itself. Clean build, no warnings: Flash 545,143/1,044,464
bytes (52.2%), RAM 37,432/81,920 bytes (45.7%) - comfortably under the
900KB ceiling. New `zxheifi_firmware.bin` is 549,296 bytes (up from
540,784 - expected, from the new field plus its load/save/API wiring).
**Reflashed and verified against the real NodeMCU on COM5**, using the
app's own `flasher.FlashJob` class (the exact code the GUI's "Flash
Firmware" button runs) - this doubled as a real-flash end-to-end test of
entry (6)'s esptool self-invoke fix, not just `read-mac`: wrote 549,296
bytes (393,238 compressed) in 10.7s, hash verified, then booted clean -
`=== ZXHEIFI NODEMCU FIRMWARE ===` / `=== SETUP MODE ===` / prompting to
connect to `ZxheiFi-Setup` WiFi, correctly falling back to Setup Mode
since this device still has no saved `/network.json`.

## 2026-09-22 (6) — Fixed: packaged desktop app couldn't run esptool at all
Real bug, found by actually smoke-testing the *packaged* portable .exe
against real hardware (not just the dev `python main.py` run - all
earlier verification in this session had only exercised the dev path).
Both `FlashJob` (flashing) and the just-added `DeviceProbe` (MAC/IP
detection) invoked esptool as `subprocess.Popen([sys.executable, "-m",
"esptool", ...])`. In dev that's fine - `sys.executable` is a real
`python.exe` with esptool installed. But in a PyInstaller-packaged
`.exe`, `sys.executable` **is the packaged app itself** - there is no
separate Python on the end user's machine to run `-m esptool` against,
so every flash and every MAC/IP detection would have silently failed on
literally any machine this was actually shipped to. This existed before
today's MAC/IP feature too; it just happened to never get caught because
nobody had smoke-tested the built `.exe` against real hardware before
this session, only the dev run.

**Fix:** `desktop-app/flasher.py` now re-invokes the app itself with a
`--esptool-worker` sentinel (`ESPTOOL_WORKER_ARG`) instead of `-m
esptool` - in a packaged build that's `[sys.executable, "--esptool-worker",
...]` (the exe calling itself), in dev it's `[sys.executable, "main.py",
"--esptool-worker", ...]`. `main.py` checks for that sentinel as the very
first thing in `if __name__ == "__main__":`, before any Tkinter/GUI code
runs, and calls `esptool._main()` in-process (the same real entry point
`python -m esptool` itself uses - not the lower-level `esptool.main()`,
so FatalError/SerialException etc. still get esptool's own clean error
formatting instead of a raw Python traceback). Since esptool is now
actually `import`ed by reachable code, PyInstaller's dependency analysis
bundles it automatically.

That surfaced a second, separate packaging gap: esptool ships its
ESP8266/ESP32 flasher stub payloads as package *data* files
(`esptool/targets/stub_flasher/*`), not Python code, so PyInstaller's
default analysis didn't bundle them - the packaged exe connected to the
chip and read its MAC fine, then failed with "Flasher stub data is
missing for ESP8266" the moment it needed to actually use the stub.
Fixed by adding `--collect-data esptool` to both PyInstaller invocations
in `desktop-app/build.py`.

**Live-tested against the real NodeMCU on COM5, packaged exe only** (not
dev mode, to actually prove the fix): `dist\...-Portable.exe
--esptool-worker -c esp8266 -p COM5 read-mac` now produces output
byte-for-byte identical to a normal `python -m esptool` run, including a
successful stub-flasher upload; the full GUI flow (launch → auto-detect
on the default port) correctly showed "MAC bc:dd:c2:47:a2:76 | no WiFi IP
yet (unconfigured or off-network)" - matching the same hardware verified
earlier via direct esptool calls. Did not re-run a full firmware flash
through the packaged exe's GUI (would have overwritten this device's
current firmware for a repeat of the exact code path already proven via
CLI); the CLI-level worker test covers the identical mechanism
`FlashJob` uses.

## 2026-09-22 (5) — Desktop app: auto-detect NodeMCU MAC/IP from the selected COM port
User asked for the Flash Firmware tab's COM port selector to automatically
show the connected NodeMCU's MAC address (and IP, if it already has one)
instead of requiring it to be typed in by hand elsewhere.

New `flasher.DeviceProbe` runs `esptool read-mac` against the selected
port - this works even on a device with no WiFi configured yet, since it
talks straight to the ESP8266 bootloader over serial (the same reset
mechanism `FlashJob` already uses for flashing), not to the running
sketch. esptool's own reset at the end of `read-mac` leaves the board
booting normally, so the probe then briefly (6s) watches the serial
console for the firmware's own `"WiFi connected, IP: ..."` line
(`nodemcu_firmware.ino:108`) in case the device is already on a network -
best-effort, since a fresh/unconfigured device boots into Setup Mode
instead and never prints it.

`FlashTab` (`desktop-app/main.py`) now runs this automatically whenever
the port dropdown changes and once on startup/Refresh, showing the
result under a new "Detected Device:" line; the Flash/Monitor buttons
are disabled for the ~5-10s the probe needs (it needs exclusive access to
the port, same as flashing). The detected MAC is also fed into the
Configure MikroTik tab's "NodeMCU MAC" field automatically
(`MikrotikTab.apply_detected_device()`) - that field feeds directly into
the static DHCP lease/ip-binding `mikrotik_commands.py` pushes to the
router, so this closes a real copy-paste-typo risk. The detected IP is
NOT auto-filled into "NodeMCU IP", since that field is the *static*
address the admin wants to assign, not necessarily whatever the device
already has.

**Live-tested against real hardware** (the NodeMCU on COM5 from this
session's earlier reflash): correctly read MAC `bc:dd:c2:47:a2:76` and
correctly reported no IP yet (this device is still unconfigured/in Setup
Mode); correctly reported "No device responded" when a Bluetooth virtual
COM port with no ESP8266 on it was selected instead.

## 2026-09-22 (4) — Desktop app: portable + installer builds
User asked for the desktop app (ZxheiFi Setup Companion) to ship as two
versions: an installable `.exe` and a portable `.exe` that runs from a
single folder with no installation. Added `desktop-app/build.py`, which
builds both from source in one command (`python build.py`, or
`--portable`/`--installer` for just one):
- **Portable** — PyInstaller `--onefile --windowed` →
  `dist/ZxheiFi-Setup-Companion-Portable.exe` (~33MB, self-contained,
  runs from anywhere).
- **Installer** — PyInstaller `--onedir --windowed` (faster startup than
  onefile, since nothing needs to self-extract to a temp dir first)
  wrapped by a new `desktop-app/installer.iss` Inno Setup script →
  `dist/ZxheiFi-Setup-Companion-Installer.exe` (Program Files install,
  Start Menu shortcut + optional desktop shortcut, proper uninstaller).
  `build.py` locates `ISCC.exe` at the conventional Inno Setup install
  path or the nonstandard one this machine actually has
  (`C:\InnoSetup6\`, see the Inno Setup install note in the
  2026-09-22 (2) entry above) or on PATH.

Both need an icon; added `desktop-app/assets/icon.ico` (multi-size,
16-256px, generated from the existing `logo.png` via Pillow — already a
dependency). Both builds compiled clean and were smoke-launched
successfully (the portable `.exe` directly; the installer's payload via
its `dist/ZxheiFi-Setup-Companion/` onedir folder, which is the exact
binary the installer packages — the installer itself wasn't run on this
machine to avoid making a real Program Files/registry/Start Menu change
as a side effect of a build verification).

## 2026-09-22 (3) — Customer-facing sound system, scrollable desktop-app tabs
User asked for two things: (1) a place for sound/music on the
customer-facing login/status pages - audio cues when inserting a coin,
plus background music - and (2) the Flash Firmware / Configure MikroTik
tabs in the desktop app to be scrollable so content below the fold
(especially the new Port Roles panel) is always reachable.

**Sound system.** Since this project can't generate real audio content,
the whole feature is file-driven: drop `.mp3` files into the new
`mikrotik/gui/sounds/` folder using the exact names documented in its
`README.md` (`background-music`, `coin-insert`, `success`, `error`,
`click`) and `script.js`'s `SoundManager` IIFE plays them - a missing
file just means that cue stays silent, never an error. Off by default
via a new `AdminAPI::soundEnabled` flag (persisted, admin-editable from
`admin.html` Settings → **Enable Sounds**, exposed unauthenticated via
`GET /api/branding` since login/status render before any admin session
exists) - with it off, the speaker icon never even appears on
login.html/status.html. With it on, each visitor still gets their own
mute toggle (the speaker icon doubles as one). Background music can't
autoplay on page load (browser policy) - it starts on the visitor's
first tap of the speaker icon or the new "Insert Coin" prompt on the
Hotspot login form, both genuine click handlers. `error` plays on a
failed login, `success` on status.html's first successful status load,
and `click` on the general navigation buttons (mode toggle, Scan QR,
Connect, Pause/Resume, Extend, Disconnect, Show QR). Mirrored in
`tools/mock_server.py` for dev-testing parity and documented in
`docs/06-gui-customize.md`.

**Desktop app: scrollable tabs.** `FlashTab` and `MikrotikTab` in
`desktop-app/main.py` now build their content inside a
`CTkScrollableFrame` instead of packing directly into the tab frame -
previously a short window could clip the bottom of a tab (most visibly
`MikrotikTab`'s Port Roles panel, added in the same session as the Rate
Profiles rework below) with no way to reach it.

## 2026-09-22 (2) — Unified Rate Profiles, coin/voucher rework, admin kick/block security fix, MikroTik port roles
User asked for the coin slot and voucher pricing to become one
admin-defined table (peso amount → minutes → validity, all in minutes
not hours/days), with the same table driving both coins and vouchers,
plus export/import, a proper admin kick/block feature, and per-port
role assignment in the desktop app's MikroTik configurator.

**Replaced the old pricing model.** Previously: fixed Tier1/2/3
(compile-time price/time/data, admin-editable but always exactly 3)
for vouchers, and a completely separate *linear* minutes-per-peso rate
for the coin slot - the two were different pricing systems. Now:
`AdminAPI::rateProfiles` (`firmware/admin_api.h`) is a single
admin-sized list of `{pesoAmount, minutes, dataMb, validityMinutes,
speedProfile}` entries that BOTH draw from - a ₱5 coin and a ₱5 voucher
always grant identical minutes/data/validity/speed. Existing
devices auto-migrate (missing `rateProfiles` in `config.json` seeds 3
entries from the old Tier1/2/3 constants) so nothing resets on
upgrade. The 3 old Tier Mbps settings survive as "Speed Profiles" -
still exactly 3 fixed MikroTik bandwidth profiles
(hs-default/gaming/high + pppoe-*), just referenced by each Rate
Profile's `speedProfile` field instead of a 1:1 tier mapping.

**Coin slot is now additive per denomination, not linear.**
`processCoinSlot()` (`firmware/nodemcu_firmware.ino`) used to convert
the *running total* credit at a flat rate (`coinCreditPhp *
coinMinutesPerPeso`) - a ₱5 coin was mathematically forced to be worth
exactly 5x a ₱1 coin. Now each physical coin-drop burst's exact peso
value is looked up via `AdminAPI::profileByPeso()` and that profile's
minutes/data are credited directly - a ₱5 coin can be worth
disproportionately more than five ₱1 coins (matches how real vendo
pricing psychology usually works: bigger coins pay a bonus rate). A
denomination with no matching profile is logged and credited nothing,
rather than guessed at - the admin is expected to define every real
coin value their acceptor takes.

**"Validity" now does double duty, reusing an already-existing
mechanism.** For vouchers it's the shelf-life before first redemption
(same as the old `voucherValidityDays`, just in minutes now). For an
in-progress session (coin or voucher) it's *also* the pause-before-
forfeit grace window - `session.h` already had exactly this
(`SessionEntry.pausedAtMs` + the old fixed `MAX_PAUSE_MINUTES`
constant); the only change needed was making it per-session
(`pauseWindowMinutes`, sourced from the matching Rate Profile) instead
of one global value.

**Found and fixed a real, exploitable auth gap while building the kick
feature.** `handleDisconnect()`/`handlePause()`/`handleResume()`
(`firmware/gui_handler.h`) take a bare session id with **no admin check
and no ownership proof at all** - and the Active Users dashboard's
existing "Kick" button called this exact self-service endpoint. That
means an admin-authorized kick and a bare unauthenticated POST were
indistinguishable server-side: anyone who merely knew (not owned) a
session id - a voucher code, printed on a receipt, visible on a
screen - could kick or pause any other paying customer's session.
Fixed by leaving `/api/disconnect` as the customer's own self-service
path (unauthenticated by design, same trust level as login - a
customer disconnecting their *own* known code is not a new risk) and
adding genuinely separate, `requireAdmin()`-gated endpoints:
`/api/admin/kick` and a new `/api/admin/block` (kicks *and* adds a
persistent RouterOS `/ip hotspot ip-binding type=blocked` entry for
that device's MAC via a new `MikrotikAPI::blockMac()`/`unblockMac()`/
`activeSessionMac()`, so the block holds at the router layer even
across a NodeMCU reboot - `/api/admin/unblock` reverses it).
admin.html's Active Users tab now has both Kick and Block buttons,
wired to the real admin endpoints instead of the old shared one.

**MikroTik per-port roles** (`desktop-app/mikrotik_commands.py`,
`main.py`): the two `.rsc` scripts and the desktop app both hardcoded
`ether1`=WAN, everything else (+`wlan1` on hAP lite)=one hotspot
bridge. Added a `port_roles` map (`wan` | `hotspot` | `pppoe` | `lan` |
`unused` per port) with defaults matching that exact old hardcoded
behavior, so nothing changes unless the admin edits the new "Port
Roles" panel in the Configure MikroTik tab. A port marked `pppoe` gets
its own dedicated interface for the PPPoE server (previously always
forced to share the hotspot interface); a port marked `lan` gets a
second, completely separate bridge/subnet/NAT with no hotspot walled
garden at all (plain, ungated internet - e.g. for the shop owner's own
PC). Verified with both the default role map (reproduces the exact old
command sequence) and a custom map matching the user's own example
(WAN+hotspot / plain-LAN / dedicated-PPPoE / hotspot ports).

**Voucher generator + shared rate file.** `vouchers/generator.py`'s
hardcoded `TIERS` dict (which its own comment admitted had to be
manually kept in sync with `config.h`) is gone - it now reads
`vouchers/rate_profiles.json`, the **same JSON shape** the admin
dashboard's new Export Rates button downloads and Import Rates
uploads. `--tier` became `--profile <pesoAmount>`. This closes the
"two independently-maintained copies of the same prices" gap for good:
export from the dashboard, drop the file next to `generator.py`, done.

**Verified**: `tools/regression_test.py` grew from 60 to 73 checks
(Rate Profile CRUD/export-import round-trip, coin-style profile
lookup via voucher generation, the kick/block auth-gating contract -
`/api/admin/kick` 401s with no creds, 200s with them, and the session
is actually gone after; `/api/disconnect` still works unauthenticated
for self-service; a blocked MAC shows up in `/api/admin/blocked`) -
73/73 clean. Firmware recompiled clean (540,784 bytes flash / 37,364
bytes RAM, still comfortably under the 900KB budget). Browser-pane
click-through against `mock_server.py`: added a ₱1/30-min/unlimited-
data profile through the new Rate Profiles table, saved it, confirmed
it round-tripped via the API, generated a voucher from it, redeemed it
(1800s = exactly 30 minutes, unlimited-data sentinel correctly set),
and confirmed via `read_network_requests` that clicking "Kick" in
Active Users hits `/api/admin/kick` (not the old `/api/disconnect`)
and the session is actually gone afterward (404 on `/api/status`).
**Not yet done**: the NodeMCU was disconnected from COM5 partway
through this session (likely just unplugged) - this update has **not**
been reflashed to real hardware yet; do that (via the Flash tab, or
raw PlatformIO) once it's reconnected. The port-roles dry-run against
the CHR/QEMU rig from earlier this session is also still pending.

## 2026-09-22 — Rebrand to ZxheiFi, logo re-theme, and the ZxheiFi Setup Companion desktop app
User asked to rename the project to their own brand "ZxheiFi" using
their own logo, and to build two companion tools: a firmware flasher
and a checkbox-driven MikroTik configurator. Confirmed first that this
was fully theirs to rebrand - per `docs/00-github-comparison.md`'s
direct audit of the real upstream repo, this codebase's architecture
and nearly every feature are original work, not a derivative of
upstream JuanFi's source. Only `mikrotik/gui/qrcode.min.js`'s existing
MIT attribution (davidshimjs/qrcodejs) stays untouched.

**Real hardware milestone this session**: the NodeMCU arrived on COM5.
Flashed the (pre-rebrand) firmware directly via PlatformIO/esptool for
the first time ever on physical hardware - confirmed a real boot log
(`=== JUANFI ENHANCED NODEMCU FIRMWARE ===`), correct fallback into
Setup Mode (no saved network config yet), and the `JuanFi-Setup` AP
genuinely broadcasting (confirmed via a live WiFi scan) - the first
non-mock, non-CHR verification this project has ever had.

**Logo re-theme**: sampled real pixel colors from the supplied logo PNG
(Pillow, not eyeballed) - deep navy background, electric cyan/teal
accent, replacing the earlier brass "Coin Meter" palette in
`mikrotik/gui/style.css`'s `:root` tokens (same variable names, so
`applyBranding()` and every existing rule kept working unchanged). The
source logo had a solid white square background with no transparency;
used a border-flood-fill (not a naive global color threshold, which
would have also erased the logo's own white wordmark text) to key out
only the true outer margin, then cropped/resized into `mikrotik/gui/
logo.png` and a matching `desktop-app/assets/logo.png`, replacing the
old inline-SVG placeholder icon on all 3 GUI pages. Found and fixed a
real leftover-brass bug while re-theming: `.btn-warning` (the "Extend
Session" button) had its background **hardcoded** to `#d9a441` instead
of referencing `var(--accent)` - invisible during the original brass
theme (looked intentional) but obviously wrong once the accent changed
to cyan. Also fixed 3 more buttons' text color and one scroll-fade
gradient that were hardcoded to the *old* theme's specific hex values
instead of using variables.

**Rebrand sweep**: ~40 files touched - firmware `#define`s (`WIFI_SSID`,
`SETUP_AP_SSID`, `NODEMCU_MDNS_NAME`, `MIKROTIK_API_USER`), the GUI's
titles/localStorage keys/mDNS hostname, both `.rsc` config scripts'
SSID/dns-name/service-name/API username, `tools/mock_server.py`,
`vouchers/generator.py`'s defaults, docs, and README. Deliberately
**not** touched: `docs/00-github-comparison.md`'s comparison table and
every dated changelog/roadmap entry (historical record of what was
true on those dates, not something to rewrite). Also fixed a stray,
inaccurate `MIT License - Ivan Alayan / JuanFi Enhanced` header comment
in `firmware/config.h` (no LICENSE file exists, and this code isn't a
fork of that author's), and a genuinely stale `500KB` firmware size
limit in `firmware/Build-Firmware.ps1`/`Makefile` that would have
rejected the actual ~532KB build (the real ceiling was already
correctly documented as 900KB in `config.h`, just never updated in
these two build scripts). Firmware binary renamed
`juanfi_enhanced_firmware.bin` → `zxheifi_firmware.bin` throughout.

**ZxheiFi Setup Companion** (new `desktop-app/`): one Python/
`customtkinter` desktop app, two tabs, packaged as ordinary `.py`
modules (PyInstaller packaging documented, not yet built as a
standalone `.exe`):
- **Flash Firmware tab**: wraps `esptool` (COM-port picker, `.bin` file
  picker, progress bar parsed from esptool's own output) plus a serial
  boot-log monitor reusing the DTR/RTS reset-pulse technique validated
  against the real hardware this session.
- **Configure MikroTik tab**: device-type radio (hAP lite/hEX), a
  network-variables form, and a feature checklist (Base Network,
  WiFi Setup [hAP lite only], Hotspot, PPPoE, Gaming QoS, Content
  Filtering, Random MAC Fix, Daily Reboot, NodeMCU API Access) executed
  over the real RouterOS API on clicking "Config" - gated behind a
  "Test Connection" step. `desktop-app/mikrotik_client.py` is a direct
  port of this session's already-validated `ros_api_test.py` (same
  `!trap`-then-`!done` fix from the 2026-09-08 entry below).
  `desktop-app/mikrotik_commands.py`'s command sequences are
  transcribed from the two `.rsc` files' own numbered sections, so the
  app and the shipped scripts never drift apart.

**Found 4 more real RouterOS command bugs this way** - all previously
undiscovered because, like the 2026-09-08 `run()` bug, nothing had ever
run these exact commands against real RouterOS before. Dry-run tested
against the CHR/QEMU rig from that same session, and fixed in **both**
`desktop-app/mikrotik_commands.py` and the two `.rsc` files so they
can't drift apart again:
1. Hotspot profile's `cookie-max-age` isn't a real parameter - the
   actual field is `http-cookie-lifetime` (confirmed against a real
   hotspot profile's own `print detail` output).
2. PPP profile's `use-vj-compression` doesn't exist in RouterOS 7.x
   (legacy PPP header compression, apparently removed) - dropped
   entirely rather than guessing a replacement.
3. `fq_codel` (underscore) isn't a valid queue `kind` - real RouterOS
   uses `fq-codel` (hyphen), and every `fq_codel-*` sub-parameter
   (`-ecn`, `-target`, `-interval`, `-quantum`) needed the same
   hyphen fix. Confirmed by successfully creating and inspecting a real
   `fq-codel` queue type on the CHR router.
4. `idle-timeout`/`keepalive-timeout` were on the wrong command
   entirely - they belong on `/ip hotspot add` (the server object), not
   `/ip hotspot profile add` (confirmed both by the profile's own
   `print detail` showing no such fields, and by successfully creating
   a real hotspot server object with them).

After all 4 fixes, re-ran the dry-run against a freshly
`/system reset-configuration`'d CHR router: 43 of 43 commands that
target an interface this minimal single-NIC, no-wireless test VM
actually has succeeded cleanly; the remaining 9 failures are 100%
attributable to referencing `ether2-5`/`wlan1` (this VM only has
`ether1`) or `/interface wireless` (CHR has no wireless stack at all,
by design - no VM can have a real radio) - a real hAP lite has all of
these, so this isn't a command defect, it's the limit of what a
single-NIC cloud router VM can ever verify.

**Verified**: firmware recompiled clean after the rebranded strings
(532,759 bytes flash / 37,036 bytes RAM - 16 bytes *less* than before,
since "ZXHEIFI" is shorter than "JUANFI ENHANCED"); full 60-check
`regression_test.py` suite clean; **reflashed the real NodeMCU on COM5
using the new flasher tool itself** (not raw PlatformIO) - confirmed on
real hardware via serial log (`=== ZXHEIFI NODEMCU FIRMWARE ===`) and a
live WiFi scan (`ZxheiFi-Setup` genuinely broadcasting); Browser-pane
screenshots of the re-themed login/status/admin pages with the real
logo rendering correctly (transparent background, no white box) against
the new navy/cyan palette.
**Not yet done**: packaging the desktop app into a distributable
`.exe` (documented, not built); the Configure MikroTik tab's dry-run
covers command-syntax correctness against real RouterOS, not the
physical hAP lite's actual WiFi radio/PoE behavior, which still needs
the user's own hardware connected via LAN + Winbox.

## 2026-09-08 — Real RouterOS API testing environment + a genuine `run()` desync bug found and fixed
User asked to keep closing out pending work, and separately asked
whether an emulator was available to get past the biggest standing
verification gap: `firmware/mikrotik_api.h`'s RouterOS binary API
client (the NodeMCU's actual command channel to the router, port
8728) had **never once been tested against a real RouterOS instance**
- `tools/mock_server.py` only mocks the NodeMCU's own JSON HTTP
contract to the browser GUI, not the RouterOS API protocol at all.
Every claim about `kickActiveHotspotUser()`, the random-MAC reconnect
fix, etc. being "correct" was reasoning from the API docs, not
verified execution.

**Built a real, free testing rig with no pre-existing emulator and no
admin/root access available**: downloaded MikroTik CHR (Cloud Hosted
Router - a real RouterOS build meant for VMs, free for this kind of
testing) and ran it under QEMU inside WSL2, entirely without `sudo` -
`apt-get download` (which doesn't require root) plus manual
`dpkg-deb -x` extraction into a user-owned prefix supplied every
package `qemu-system-x86` needed (qemu-utils, seabios for the PC BIOS
blob, and about a dozen transitive shared libraries chased down one
`ldd` pass at a time). Boots under pure software emulation (TCG, no
KVM - the WSL user isn't in the `kvm` group and granting that needs
root) in about 2 minutes, which is fine for protocol testing. A small
Python bridge (`serial_bridge.py`) keeps one persistent connection to
QEMU's serial console via a Unix socket + FIFO, since RouterOS's
console does VT100 cursor-position detection (`ESC[6n`) that a plain
socket client has to answer or the login sequence hangs.

Then wrote `ros_api_test.py`, a Python client implementing the exact
same wire format as `mikrotik_api.h` (length-prefixed words, `=key=
value` attributes, `!re`/`!done`/`!trap` sentences) and driving the
same command sequences the firmware issues, against this real CHR
instance instead of a JSON mock.

**Found a real, previously-invisible bug this way**: `MikrotikAPI::run()`
returned immediately on seeing a `!trap` reply, without reading the
`!done` sentence that RouterOS *always* sends right after a `!trap`
(confirmed on the wire against real RouterOS 7.16.2, not just assumed
from the docs). That leftover `!done` stays unread in the TCP buffer,
so the *next* API call's first `readReply()` picks it up instead of
its own command's actual reply - silently shifting every reply after
the first router-side rejection by one sentence, for the rest of that
persistent connection's life (`MikrotikAPI` keeps one long-lived
`_client` across calls). Reproduced exactly: after one deliberately
failing command, `removeHotspotUser()` reported success while the
object stayed in the router's table, and later queries returned
completely unrelated rows (a PPP profile list where a PPP secret list
was expected). Any real-world RouterOS-side rejection - a typo'd
profile name, a stale `.id`, literally any `!trap` - would have
silently corrupted every subsequent command until the device's next
reconnect. `tools/mock_server.py` could never have caught this; it
doesn't implement the real protocol's multi-sentence trap+done
behavior at all.

**Fix**: `run()` now keeps looping through a `!trap` (tracking that
the command failed) instead of returning immediately, and only
returns once the guaranteed trailing `!done` actually arrives -
draining the reply fully so the connection never desyncs. See
[mikrotik_api.h](../firmware/mikrotik_api.h)'s `run()`.

**Verified**: `ros_api_test.py` re-run against the same live CHR VM
after the fix - 19/19 checks pass (login, `addHotspotUser`,
`removeHotspotUser` incl. actually-gone-after-remove, the
no-active-session early-return path in `kickActiveHotspotUser`,
`addPppSecret`/`removePppSecret`, `getActiveTraffic`'s empty-rows
branch, and the deliberate-trap case no longer desyncing anything
after it). Firmware recompiled clean (532,775 bytes flash / 37,052
bytes RAM - 16 bytes over the pre-fix build, RAM unchanged); full
60-check `regression_test.py` suite re-run clean.
**Not live-tested** on real hardware - this closes the "protocol
correctness against a real router" gap specifically, not the
"real ESP8266 + real phone + real MikroTik box" gap, which still
needs physical hardware. The CHR VM and test scripts are scratch/dev
artifacts, not part of the shipped project.

## 2026-09 — Full GUI visual redesign ("Coin Meter" theme)
User asked to move on to the full visual redesign approved earlier
(not incremental polish on the old teal glass-morphism theme).
Researched the original JuanFi's actual latest template CSS/login page
directly rather than guessing, and found a consistent "coin-op utility
meter" vocabulary across every version it ever shipped: dark blue-gray
palette, thick borders, a signature rainbow gradient divider, blinking
connection-status text, big bold numbers with small labels. Used that
as the grounding subject for the redesign instead of a generic dark-
SaaS default.

New palette: brass/amber (`--accent`, rebrandable, replacing the old
generic teal `#00d4aa`) plus fixed, non-rebrandable `--peso` (green,
success/money) and `--alert` (red, danger/expired) — a shop's brand
color shouldn't determine whether "online" reads as good or bad.
System fonts only, no Google Fonts/CDN of any kind — a hard technical
requirement, not a style preference: `login.html`/`status.html` are
shown to a customer who is still walled off from the internet, so an
external font request would just hang. Monospace/tabular-nums for
every "meter readout" number (time/data remaining, voucher codes, peso
amounts, admin stat cards) - a deliberate choice tied to the actual
subject matter. Signature element: the voucher code input styled as a
literal coin slot (dark inset pill, monospace, letter-spaced) - the one
place this redesign spends its boldness; a quiet 2-stop brass→peso
gradient bar across each card's top edge nods to the original's
4-color rainbow divider without copying it directly.

Structural HTML/class names were deliberately left unchanged - this is
a CSS-level redesign, not a markup rewrite, so `script.js`'s DOM logic
needed no changes beyond fixing 2 stray hardcoded colors (the "online"
status dot was reading the rebrandable `--accent` and a leftover
hardcoded `#e17055` instead of the new fixed `--peso`/`--alert`).
Caught and fixed a real bug during testing: `AdminAPI::brandColor`,
`mock_server.py`'s default settings, and the admin.html color picker's
default were all still the OLD teal - since `applyBranding()`
unconditionally overrides the CSS default with whatever `/api/branding`
returns, the new palette was completely invisible in a real browser
until these three were updated to the new brass default too.

**Verified**: real browser screenshots across all 3 pages (login,
status including the QR modal, admin's Overview/Vouchers tabs) at both
desktop and mobile (375px) widths - the meter-readout numbers, coin-
slot input, and existing mobile-responsiveness fixes (tabs chevron,
tier-price-row stacking) all held up correctly. Firmware unchanged
this round (pure GUI files); full 60-check regression suite re-run
clean.
**Not live-tested** on real device/browser diversity beyond this
session's Chromium-based Browser pane - worth a look on Safari/iOS
given phones are the primary customer device.

## 2026-09-04 — Random MAC reconnect fix, real QR generation, Telegram notifications
Continued the original-JuanFi-audit punch list. Three items closed:

1. **Random MAC reconnect handling** - confirmed this project WAS
   affected by the same problem the original's `isRandomMacSyncFix`
   addresses: a phone randomizing its MAC mid-session re-triggers
   MikroTik's walled garden under a new MAC, which would have been
   rejected twice over - once by `AdminAPI::redeemVoucher()`'s
   single-use check (the voucher's already marked used from the first,
   still-active login), and again by the hotspot profile's
   `shared-users=1` blocking the new MAC while the old MAC's stale
   `/ip/hotspot/active` entry still occupies the slot. Fixed with a new
   `MikrotikAPI::kickActiveHotspotUser()` (deliberately NOT folded into
   the general `addHotspotUser()` path, since Extend uses that too and
   kicking a still-connected client mid-extend would be a regression)
   plus a reconnect branch in `handleLogin()` that resumes the existing
   session's current remaining budget instead of re-redeeming.
2. **Real QR code generation** for "Show QR" (previously text-only) -
   vendored the original JuanFi's own `qrcode.min.js`
   (davidshimjs/qrcodejs, MIT), fetched directly from its repo and
   scanned for `eval`/network/cookie access before use rather than
   trusted blindly. Verified with genuine round-trip decoding: generated
   a real QR in a live browser, decoded the canvas with an independent
   decoder (jsQR, used only for this one-time verification), and
   confirmed the decoded text exactly matched the session code.
3. **Telegram notifications** - a real gap the previous (pre-audit)
   comparison doc had incorrectly claimed was "preserved." Added
   NodeMCU-side HTTPS to the Bot API (`firmware/telegram.h`), since this
   project's architecture doesn't use the RouterOS on-login scripts the
   original sends from. Messages are queued and sent one at a time from
   the main loop, never inline in a request handler, since the 1-3s TLS
   handshake would otherwise delay a customer's own login response.
   Fires on genuine new sales (voucher/subscriber/coin) only. Admin-
   editable bot token/chat ID via Settings, unlike the original's
   config-file-only approach.

Firmware recompiled clean at each step (final: 532,759 bytes flash /
37,052 bytes RAM - the BearSSL/TLS stack for Telegram is the single
largest flash addition this project has made, pushing past the old
500KB soft budget, accordingly raised to 900KB to reflect the real
~1MB partition ceiling; still only ~51% used). Full 60-check regression
suite clean throughout.
**Not live-tested**: the random-MAC fix needs a real MikroTik router to
confirm `shared-users=1` behavior; QR generation was verified in
software only (no physical phone camera); Telegram was not tested
against a real bot token/chat.

## 2026-09-04 — First-boot Setup Wizard + original-JuanFi audit
User asked for a thorough re-check against the actual original JuanFi
repo (github.com/ivanalayan15/JuanFi) — not just the existing (partly
inaccurate) comparison doc — with the explicit goal that this project
should be a superset of the original's real functions/features, not a
replacement of some of them. Fetched the real repo's file tree, README,
and on-login MikroTik script directly (the previous `docs/00-github-
comparison.md` had never actually been verified against the source and
made at least one flatly wrong claim — "Telegram API preserved" when
this project never implemented Telegram at all).

That audit surfaced the single biggest practical gap: the original
ships a **first-boot setup wizard** (NodeMCU broadcasts its own WiFi AP,
serves a config form, no reflashing needed per deployment) while this
project required hand-editing `firmware/config.h` and recompiling for
every WiFi/MikroTik-credential change. Brainstormed and built this as
an architectural addition (see `docs/12-setup-wizard.md` for the full
design):

- **`firmware/network_config.h`** (new) — `/network.json` persistence
  for WiFi SSID/password, MikroTik host/API user/password, and an
  initial admin dashboard password; zero-migration (a device with no
  saved config just falls back to the existing `config.h` constants,
  same pattern as the salted-password work).
- **`firmware/setup_mode.h`** (new) — `SetupModeManager`: AP mode +
  `DNSServer` captive-portal redirect (any URL lands on the setup form)
  + one embedded HTML form, both bundled with the `esp8266` Arduino
  core (no new external dependencies). Enters Setup Mode automatically
  on a genuinely fresh device (no `/network.json` yet), or whenever the
  onboard FLASH button (GPIO0, already present on most NodeMCU dev
  boards) is held during boot — the only reconfiguration path,
  deliberately software-reset-free, since a dashboard button would be
  unreachable in exactly the scenario (bad saved network config) where
  you'd need it most.
- **`MikrotikAPI`/`AdminAPI`** gained runtime-configurable
  host/credentials and an optional initial-admin-password parameter,
  both defaulting to the existing `config.h` constants when no wizard
  config exists.
- **mDNS** (`juanfi-nodemcu.local`, `ESP8266mDNS`, also bundled) —
  since the wizard makes the NodeMCU's network config genuinely dynamic
  now, a hardcoded IP in `script.js` (even the one just fixed in the
  cross-origin bug below) would only ever have worked if an admin
  separately configured a matching static DHCP lease — the exact kind
  of two-places-must-match fragility that caused that bug in the first
  place. `apiBase()` now targets the mDNS hostname instead.

The audit also corrected `docs/00-github-comparison.md` with an
honest, feature-by-feature status table (✅/⚠️/❌/➖) instead of the old
version's unverified claims, and flagged several other real gaps for
future prioritization: Telegram notifications (still not implemented -
was never actually "preserved"), a physical "Night Light" relay
control (different from this project's Night Promo *pricing* discount
— same "night" name, different feature entirely), remote restart via
HTTP, real QR code generation (the original vendors a known library,
`qrcode.min.js` — worth adopting the same approach now that a
known-good library is identified, rather than the hand-rolled encoder
this project previously declined as too risky), LAN/Ethernet and ESP32
hardware options, and multi-vendo support.

Firmware recompiled clean (423,991 bytes flash / 36,236 bytes RAM —
up from ~393KB, the cost of bundling `DNSServer`+`ESP8266mDNS`, still
well under the 500KB budget). Full 57-check regression suite re-run
clean (Setup Mode itself isn't exercisable through the existing
`mock_server.py`/`regression_test.py` HTTP-contract tooling, since it's
pure boot-time AP behavior with no HTTP API surface to mock — the
regression suite instead confirms nothing in the existing `/api/*`
contract regressed from the `MikrotikAPI`/`AdminAPI` signature changes
this touched).
**Not live-tested** — no real NodeMCU hardware in this session to
confirm AP-mode captive-portal detection or mDNS resolution against
real client devices; this and the GUI's planned full visual redesign
(next up, per the user's direction) are the two biggest remaining
unknowns before a real deployment.

## 2026-09-04 — Critical fix: cross-origin GUI ↔ NodeMCU communication
User noticed something looked wrong: voucher generation appeared to be
"saved to MikroTik instead of NodeMCU." That observation pointed at a
real, severe, previously undetected bug — arguably the single most
important fix in the project's history.

**The bug**: the GUI (`login.html`/`status.html`/`admin.html`) is
served BY the MikroTik router, while the JSON API it calls is
implemented by the NodeMCU — two different devices, two different IPs
(`10.0.0.1` vs `10.0.0.254`). Every `fetch('/api/...')` call in
`script.js` used a plain relative path. A relative URL resolves
against the page's own origin — so in production, every single API
call (login, status polling, pause/resume/extend, the entire admin
dashboard including voucher generation) would silently target the
MikroTik router's own webserver instead of the NodeMCU. RouterOS's
hotspot server has no such routes, so these calls would 404 or hit
nothing meaningful — the NodeMCU would never receive them at all. This
means the **entire dynamic backend of the whole system was unreachable
in any real deployment**, undetected through every phase of this
project because `tools/mock_server.py` happens to serve both the
static GUI and the API from the same origin, masking the bug
completely in every test done so far.

**The fix**: `script.js` gained `apiBase()` — every `/api/*` call now
targets the NodeMCU's IP explicitly (`NODEMCU_IP` constant, kept in
sync with `firmware/config.h`'s `NODEMCU_IP`), falling back to a
relative path only when the page is already being served directly
from the NodeMCU or from `mock_server.py` (dev/testing). Because this
makes every real request genuinely cross-origin, `firmware/gui_handler.h`
gained CORS support: an allow-all `Access-Control-Allow-Origin` header
on every response (safe here — this is a local captive-portal API with
no cookie/session auth to steal cross-site; the real reason CORS is
needed is the two-device split, not a security boundary being
loosened) plus an `onNotFound()` handler answering the browser's
preflight `OPTIONS` requests. `tools/mock_server.py` mirrors both so
the fix stays testable without hardware.

Firmware recompiled clean (393,019 bytes flash / 35,128 bytes RAM).
**Verified with real cross-origin browser testing** — two separate
`mock_server.py` instances on different local ports (genuinely
different origins, not just a same-origin simulation): a plain GET
across origins succeeded, and critically, a POST-equivalent GET
carrying the `X-Admin-Username`/`X-Admin-Password` headers (which
triggers a real CORS preflight) also succeeded, proving the preflight
+ header combination actually works end to end. Separately verified
`apiBase()`'s decision logic covers every real hostname correctly:
relative for the NodeMCU's own IP and dev/testing hosts, absolute for
MikroTik's `hotspot-address` and `dns-name`. Full regression suite
re-run clean afterward.
**Not live-tested** — no real MikroTik/NodeMCU hardware available in
this session to confirm the actual production topology (two real
devices on a real 10.0.0.0/24 network) behaves identically to this
two-mock-server simulation.

## 2026-09-04 — Salted password hashing
Continued down the "do what's possible without real hardware" list.
Admin (`AdminAccount`) and subscriber (`Subscriber`) passwords were
stored as plain `MD5(password)` — no salt, meaning a stolen
`admins.json`/`subscribers.json` could be attacked with a precomputed
rainbow table. Added a random per-account `salt` field (from
`RANDOM_REG32`, the ESP8266's hardware RNG register — not Arduino's
predictable seeded `random()`), stored hash becomes `MD5(salt +
password)`.

Designed for zero-migration backward compatibility: every account that
already exists has `salt = ""`, and `MD5("" + password)` is exactly
the same value as the old `MD5(password)` — so no stored hash needs
re-computing and no admin/subscriber needs to reset their password.
The default `admin`/`ADMIN_PASSWORD_HASH` account seeded on first boot
is unaffected. Only accounts added *after* this change get a real
random salt.

Deliberately scoped to salting only — not a switch to a proper
password KDF (bcrypt/argon2/scrypt), which would be a much larger
change (new library, real per-login compute cost on an ESP8266) and
wasn't asked for.

Firmware recompiled clean (392,719 bytes flash / 35,016 bytes RAM).
Backward-compatibility verified by reproducing the exact
`ADMIN_PASSWORD_HASH` constant from `MD5("" + "admin")` in Python, and
confirming a random salt produces a different hash for the same
password. The full `tools/regression_test.py` suite (57 checks) was
re-run afterward with no failures.
**Not live-tested** — no real ESP8266 hardware in this session to
confirm `RANDOM_REG32` behaves as documented on actual silicon.

## 2026-09-04 — Post-audit fixes: live tier labels, brute-force lockout, regression test
User asked for another audit pass plus prioritized suggestions, then
asked to work through whatever could be done without real hardware.
Three items shipped from that list:

1. **Dynamic tier dropdown labels.** The audit re-confirmed a real,
   growing staleness problem: `admin.html`'s Vouchers/Subscriptions/
   Coin Slot tier dropdowns were still hardcoded text ("₱10 - 1hr /
   500MB", "Tier 1 (5M/10M)") even though price/time/data (from
   earlier work) and now Mbps are Settings-editable — an admin who
   changed Settings would see stale numbers everywhere else in the
   dashboard. Fixed with a new `GET /api/admin/tier-info` endpoint
   (`GUIHandler::handleAdminTierInfo()`) returning just the display
   numbers, deliberately open to **both** roles (unlike the full
   `/api/admin/settings`, super-only) since staff generates vouchers
   too and needs accurate pricing. `script.js`'s `loadTierLabels()`
   fetches it once on admin page load and rewrites all 9 option
   elements' text.
2. **Login brute-force protection.** Neither `/api/login` nor admin
   auth had any rate limiting — an attacker could script unlimited
   voucher-code or admin-password guesses. Added a small fixed-size
   per-IP failure table shared by both (`GUIHandler`'s `LoginAttempt`
   array, `config.h`'s `LOGIN_MAX_FAILURES`/`LOGIN_LOCKOUT_MS`/
   `LOGIN_ATTEMPT_TABLE_SIZE`): 8 failures from the same IP within a
   1-minute window get `429 too_many_attempts` on every further
   attempt, credentials unchecked, self-clearing once the window
   passes. Careful to only count a **genuine** failed voucher/
   subscriber attempt (not the normal "this code isn't a subscriber
   username" fallthrough every plain voucher login hits).
3. **Automated regression test** (`tools/regression_test.py`) — drives
   `mock_server.py`'s full `/api/*` contract (57 assertions: login/
   pause/resume/extend/disconnect, admin auth + role gating, settings,
   voucher/subscriber/admin-account CRUD, the new tier-info endpoint,
   and the new lockout) via plain HTTP requests, no browser needed.
   Built to catch exactly the kind of contract regression a future
   change could introduce silently. **Found and fixed a real bug in
   its own first draft**: running `mock_server.py`'s handler in-process
   via a threaded `ThreadingHTTPServer` sharing the GIL with a
   synchronous test client deadlocked on certain requests (`/api/resume`
   specifically, in testing) — switched to launching `mock_server.py`
   as a real subprocess instead, which resolved it and better matches
   how the tool is actually used elsewhere in the project.

QR code generation for "Show QR" (status.html) was **deliberately not
attempted** in this pass — a correct from-memory QR encoder (Reed-
Solomon error correction, mask evaluation, module placement) carries
real risk of a subtly broken implementation that looks fine but doesn't
scan, which is worse than the current honest text fallback. Left as a
flagged gap for when a properly-vetted library can be pulled in, rather
than shipping something unverified - consistent with this project's
existing stance on the QR *scanner* (`BarcodeDetector`, not a
hand-rolled decoder).

Firmware recompiled clean (392,207 bytes flash / 35,000 bytes RAM).
Verified: brute-force lockout tested directly via curl (8 failures ->
400, 9th -> 429, including blocking an otherwise-valid code and
blocking admin auth from the same IP); tier-info label sync verified
in a real browser (changed Tier 1 via the Settings API, confirmed all
3 dropdowns picked up the new numbers, confirmed a staff account can
read tier-info but still gets 403 on full Settings); the new regression
suite passes all 57 checks end to end.
**Not live-tested** — no real MikroTik/NodeMCU hardware available in
this session.

## 2026-09-04 — Bug sweep: walled-garden login + subscriber password mismatch
User asked to fix "all the bugs" after the Bandwidth settings work. A
systematic re-read of the session/pppoe/gui_handler/script.js login
path (not just the files touched that day) found two real,
previously-undetected functional bugs, both severe enough that they'd
have affected every real deployment on actual hardware:

1. **Missing walled-garden release (hotspot mode).** `/api/login`
   only ever registered the account with MikroTik via the RouterOS API
   (NodeMCU-to-router, backend-to-backend) - it never made *this
   specific browsing client* actually authenticate against MikroTik's
   own hotspot login handler, which is what RouterOS requires before it
   releases a walled client's traffic. Nothing in `login.html`/
   `script.js` ever submitted to the router's real `/login` endpoint.
   In practice, a customer would see "Connected" on our own status
   page while still being stuck behind MikroTik's captive portal with
   no actual internet - the single most consequential bug found in this
   project. Fixed: `script.js` now submits a real (non-AJAX) form POST
   to `/login` with `username`/`password`/`dst` right after `/api/login`
   succeeds, for hotspot mode only (`submitMikrotikHotspotLogin()`) -
   `dst` points RouterOS back at our own `status.html` instead of its
   default post-login page. PPPoE mode is unaffected by design (that's
   a separate dial-up connection outside the browser). The same
   re-authentication now also happens on **Resume** (`togglePause()`),
   since Pause fully deprovisions the account and kicks the active
   walled-garden session, so Resume needs to redo it too, not just Login.
2. **Subscriber accounts got the wrong MikroTik password.**
   `PPPoEManager::provision()` always created the MikroTik hotspot-user
   / PPP secret with the session code as **both** username and password
   - correct for vouchers (which don't have a separate password) but
   silently wrong for subscribers, whose whole point is a real,
   admin-set password distinct from their username. A subscriber's
   actual PPPoE dial-up, or the walled-garden login now added above,
   would have failed to authenticate with the password they were
   actually told to use. Fixed: `provision()` gained an optional
   `password` parameter (defaults to the session code, so voucher/coin
   callers are unaffected); `handleLogin()`'s subscriber branch now
   passes the real (already-verified) password through, and
   `handleResume()`'s request body gained an optional `password` field
   for the same reason, since Resume recreates the account and can't
   recover a subscriber's plaintext from just their stored MD5 hash.
   `script.js` now caches the login password client-side
   (`juanfi_password` in localStorage, empty for vouchers) precisely so
   Resume can resend it without asking the customer to retype it.

Also fixed while re-reviewing the Bandwidth settings work from earlier
today: the `mikrotikPushed` JSON response buffer (`DynamicJsonDocument
out(64)`) was tight enough to risk silently dropping a field - bumped
to 128, matching `handleHealth`'s established sizing for a 2-field
response; `AdminAPI::rateLimitStringFor()` rounded fractional Mbps
values to whole numbers (`String(x, 0)`) before pushing them to
MikroTik, silently saving a different bandwidth cap than what Settings
displayed - fixed with a proper `mbpsToken()` formatter; and the
`if (_mikrotikReachable)` gate before pushing Mbps was removed since it
was inconsistent with every other MikroTik-touching handler in this
codebase (they all just attempt the call and let its own return value
decide success) and could report a false "not pushed" off a flag that's
only refreshed every 30s.

Firmware recompiled clean (391,287 bytes flash / 34,760 bytes RAM).
Verified end-to-end in a real browser against an updated
`tools/mock_server.py` (which gained a `/login` route simulating
MikroTik's own redirect-on-success shape, so this flow stays testable
without real hardware): a voucher hotspot login now correctly lands on
`status.html` via the `/login` redirect (not directly), a subscriber
login (`room5`/`tenant123`) does the same, and Pause→Resume correctly
re-authenticates and returns to `status.html` for both. Direct
programmatic calls to `submitLogin()` were used alongside real
click-driven UI tests, since dynamic `ref_N` element references from
`read_page` occasionally went stale mid-flow during manual testing (a
tooling quirk, not a product bug).
**Not live-tested** — no real MikroTik router available in this
session to confirm actual RouterOS `/login` behavior (CHAP challenge
handling, exact redirect semantics) matches the mock's simplified
simulation; the fix follows RouterOS's documented `username`/
`password`/`dst` POST convention and the existing `.rsc` configs'
`login-by=http-chap,http-pap` (which permits plain PAP, no CHAP
challenge required).

## 2026-09-04 — Bandwidth (Mbps) settings
User asked to double-check that coin slot/voucher Settings had
validity/expiration, and whether coin slot, voucher, and PPPoE had
Mbps (bandwidth) settings. Audit: voucher validity was already working
(no gap); coin slot validity doesn't apply the same way since coin
credit converts to a session instantly rather than sitting as a
pending, expirable state (flagged to the user, no change made without
confirmation); **Mbps settings were a real gap** — bandwidth was
entirely hardcoded in the MikroTik `.rsc` files with no admin control.

Added Download/Upload Mbps per tier to Settings, per the user's two
choices: **shared per tier** (one pair of values applies to both the
hotspot and pppoe profile of that tier — covering coin slot, voucher,
subscriber, and PPPoE sessions at once, since they already share the
same tier→profile mapping) and **live-pushed via the RouterOS API**
(not just saved for reference) — `MikrotikAPI` gained
`setHotspotProfileRateLimit()`/`setPppProfileRateLimit()`, called on
every Settings save so a bandwidth change takes effect immediately,
even for already-connected users. The GUI reports whether the push
actually reached the router (`mikrotikPushed`), since the NodeMCU and
MikroTik can be temporarily out of touch.

Firmware recompiled clean (391,071 bytes flash / 34,776 bytes RAM).
Verified end-to-end against `tools/mock_server.py`: correct defaults
loaded (10/5, 30/10, 50/20 Mbps, matching the `.rsc` files), a changed
value saved and persisted, and the mobile layout (reusing the existing
`.tier-price-row` class) stacked correctly with no overflow at 375px.
One real bug caught during verification: the new fields were wired
into `saveSettings()` but initially missed in `loadSettingsIntoForm()`,
so the Settings tab loaded them blank — found by checking actual input
values in the browser, fixed before calling it done.
**Not live-tested** — no real MikroTik router available in this
session to confirm the actual `/set` API calls land correctly.

## 2026-09-03 — Ad-hoc additions (post-Phase D follow-up requests)
Four follow-up requests arrived after the Visual GUI review, each
implemented directly rather than folded into a new lettered phase.
Full detail (exact fields, file-by-file changes, verification steps)
lives in [10-feature-roadmap.md](10-feature-roadmap.md)'s decisions
log — this is the summary:

- **Coin slot: proportional pricing.** Replaced the old fixed-tier
  model (credit had to reach a full ₱10/20/30 package to grant
  anything) with a proportional peso→minutes/MB rate, admin-editable
  in Settings (`coinMinutesPerPeso`, `coinMbPerPeso`, `coinQosTier`).
  A ₱5 drop now grants 5 pesos' worth of access immediately.
- **Voucher validity + customizable tier pricing.** Added
  `voucherValidityDays` (0 = never expires) and made Tier 1/2/3
  price/time/data admin-editable via Settings instead of firmware
  constants — both baked into each voucher/CSV row at generation time,
  never retroactive to already-issued vouchers, matching this
  project's established design principle.
- **Voucher ticket design customization.** `vouchers/generator.py`
  gained `--brand-name`, `--accent-color`, `--footer-text` flags
  (chosen over syncing branding from the NodeMCU, which would need a
  new sync mechanism for an offline tool); `print_template.html` reads
  the accent color via a new `{{ACCENT_COLOR}}` token.
- **Mobile-responsive admin dashboard.** Fixed two real layout bugs
  found via actual mobile-viewport screenshots: the new 3-input tier-
  pricing rows overflowed their card at 375px (fixed with a
  `.tier-price-row` class + a mobile media query), and the 7-tab admin
  nav had no visible sign that more tabs existed off-screen (an
  initial `mask-image` fade was confirmed applied but invisible against
  the dark theme; replaced with an always-visible `›` chevron).

Firmware recompiled clean (389,643 bytes flash / 34,556 bytes RAM).
Verified end-to-end against an updated `tools/mock_server.py`: expired-
voucher rejection on login/extend, live-Settings voucher generation,
and non-retroactivity of already-issued vouchers. `generator.py`
re-run with all new flags — QR codes still decode correctly and every
customization string appears in the output.
**Not live-tested** — no real MikroTik/NodeMCU/coin-acceptor hardware
available in this session.

## 2026-09-03 — Visual GUI review
All GUI work through Phase D had only been verified via JS/DOM checks,
never an actual screenshot. The user asked to see the real UI, and a
screenshot pass across all 3 pages + 7 admin tabs found 3 real CSS bugs
no functional test would catch: `.login-form` missing
`flex-direction:column` (fields/buttons overlapping horizontally), 11
more classes referenced but never defined in `style.css` (on top of 5
already found in Phase C), and `body` missing a base text color (black
text on a near-black background, essentially invisible wherever
nothing else overrode it). Full detail in
[10-feature-roadmap.md](10-feature-roadmap.md). CSS-only, no firmware
changes.

## 2026-09-03 — Phases A, B, C, D of the feature roadmap
Full detail (scoping decisions, exact file-by-file changes, test
results) lives in [10-feature-roadmap.md](10-feature-roadmap.md)'s
decisions log — this is just the summary.

- **Phase A** (MikroTik-side): Content Filtering (DNS-based, always-on,
  new `mikrotik/content_filter.rsc`), Gaming QoS extended for Mobile
  Legends/COD Mobile's dynamic UDP ports, WiFi coverage extension docs
  for both routers. Also fixed a real bug found along the way: `hEX_
  full_config.rsc` referenced a bridge interface it never created.
- **Phase B** (session logic): per-tier Time-only/Data-only mode,
  manual Pause/Resume (actually cuts MikroTik access, 2-hour max
  pause), and a Subscription system (admin-managed billing, unlimited
  while active, cuts off immediately on expiry). Also fixed a real
  compile bug found by recompiling after the change: `ESP8266WebServer::
  collectHeaders()` is a variadic template on this core version, not
  the `(array, count)` form used earlier.
- **Phase C** (admin/ops + GUI): Multi-admin (super/staff roles,
  dynamic accounts, last-super-admin lockout protection), a rolling
  300-event Activity Log, a 90-day Sales Inventory split by payment
  method, and a no-code Rebrandable Portal (business name + accent
  color, applied live via a new `--accent` CSS variable). Also fixed a
  real pre-existing bug found while extending the CSS: `.stats-grid`,
  `.stat-card`, `.table-container`, and `.toggle-switch` were used
  throughout every GUI page since the very first session but never
  actually defined in `style.css`.
- **Phase D** (coin acceptor): turned out small — the user's acceptor
  is a single proportional-pulse type, which the existing single-GPIO
  design already fit. Added `docs/11-coin-acceptor-wiring.md` and two
  defensive checks (`MIN_PULSE_INTERVAL_US` rejects noise pulses,
  `MAX_PLAUSIBLE_PULSES` rejects and logs implausible bursts instead of
  crediting them). Found a more significant pre-existing bug while
  there: the coin-slot debounce compared against a timestamp it only
  updated inside its own "keep waiting" branch, so once a poll cycle
  fell through to processing (which happened on almost the first pulse
  of nearly every coin), it never re-armed — the *total* peso credit
  still ended up correct, but the buzzer fired multiple times per coin
  and any burst-size check would have seen fragments, not the real
  burst. Fixed by having the interrupt handler itself stamp the
  timestamp on every accepted pulse.
- Multi-Currency and Multi-Language were scoped out of Phase C at the
  user's request (see roadmap doc for why).
- Firmware size: 380,864 bytes after Phase B, 387,315 after Phase C,
  387,587 after Phase D — all recompiled and verified with PlatformIO,
  all well under the 500KB budget. Phases B and C were also tested
  end-to-end in a real browser against `tools/mock_server.py`; Phase D
  had no browser-testable surface (pure firmware), so it was verified
  by compiling plus manually tracing the pulse-burst timeline.
- **Not live-tested** — no real MikroTik/NodeMCU hardware in any of
  these sessions.

## 2026-08-25 — Firmware/GUI overhaul (made the "Enhanced" features real)
A review found that only the MikroTik `.rsc` layer of the original
scaffold actually worked; the NodeMCU firmware, GUI, and voucher
tooling were incomplete or broken. This pass:

**Fixed (broken → working):**
- `firmware/nodemcu_firmware.ino` referenced 4 nonexistent `.cpp`
  modules and 8 undefined functions — could not compile at all.
- `firmware/session.cpp` was pseudocode/markdown saved with a `.cpp`
  extension, containing an invalid 18-bit `uint16_t` bitfield.
- `mikrotik/gui/script.js` was truncated mid-function and missing
  every handler the 3 GUI pages call.
- `mikrotik/gui/admin.html` had a malformed `<button>` tag and a CSS
  bug that hid the Overview tab on page load.
- `mikrotik/hEX_full_config.rsc` referenced a bridge interface it
  never created, and was missing the Gaming QoS section present in the
  hAP Lite config.
- `firmware/Makefile` contained a non-functional shell command chain.
- Arduino build-system bug: `#include`-ing another `.cpp` file from
  the `.ino` causes duplicate-symbol linker errors, since Arduino
  auto-compiles every `.cpp` in the sketch folder as its own
  translation unit. All modules are now header-only (`.h`).

**Added (didn't exist before):**
- `firmware/mikrotik_api.h` — RouterOS API client (binary protocol,
  port 8728): login, generic command execution, and helpers for
  Hotspot users / PPP secrets.
- `firmware/session.h`, `qos.h`, `pppoe.h`, `admin_api.h`,
  `gui_handler.h` — Time+Data combo tracking, Idle Auto-Pause, Night
  Promo, dual-mode provisioning, voucher store, admin dashboard API.
- `vouchers/generator.py` + `print_template.html` — voucher code/CSV/
  printable-QR-ticket generator (tested: generated QR codes were
  decoded and confirmed to match the printed voucher text).
- `docs/03` through `docs/09` — previously referenced by
  `01-overview.md` but never written.

**Verified in this session:**
- `vouchers/generator.py` run end-to-end; QR payloads decoded correctly.
- `mikrotik/gui/script.js` passes `node --check`.
- Full firmware compiled successfully with PlatformIO
  (espressif8266/nodemcuv2, ArduinoJson ^6.21.0): 370,323 bytes flash
  (35.5% of budget), 32,700 bytes RAM (39.9%), 0 errors (a handful of
  harmless SPIFFS-deprecation warnings — SPIFFS still works, just
  superseded by LittleFS upstream).
- Packaged binary copied to `firmware/juanfi_enhanced_firmware.bin`
  (374,480 bytes) — built from `config.h`'s default/placeholder
  credentials, so it must be rebuilt after editing them for real use.

**Not verified (no hardware available in this session):**
- Flashing onto a physical NodeMCU.
- End-to-end RouterOS API behavior against a real MikroTik router.
- In-browser QR generation for the "Show QR" share panel (text
  fallback only — see `docs/08-faq-troubleshoot.md`).
