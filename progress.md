# 📊 PROJECT PROGRESS — ZXHEIFI

## Development Status
**Last Updated:** 2026-09-26

- **v1.0.0 release (2026-09-26 ~10:30) - done.** Unified version 1.0.0 (firmware/GUI/app/installer). Starter sounds (7 MP3s) in mikrotik/gui/sounds. Guide tab English/Tagalog toggle + "problems we hit" section. MIT LICENSE + README rewrite + THIRD-PARTY-NOTICES + .gitignore, credit to JuanFi (Ivan Julius Alayan, not affiliated). Docs 06/07/08/09 updated, new docs/13-v2-masterplan.md (sub vendo, charging station, e-wallet, Fair Time, remote mgmt, ESP32). Final audit: firmware 554,879 B clean build, test_configurator ALL PASSED, regression 103/103, browser flow on mock OK. Release folders via tools/make_release.py -> release/ZxheiFi-v1.0.0/{Source Code, Installer, Portable}. USER NEXT: reflash NodeMCU with 1.0.0, Upload GUI Files, enable sounds, coin acceptor test. Nothing committed to git.
- **2026-09-26 09:05:** real NodeMCU flashed with 2026.09.26.2 (open-WiFi join fix), online at 10.0.0.254 and talking to MikroTik. Installer (09:07) and Portable (09:10) rebuilt. User next: Upload GUI Files, refresh phone, admin login.
- **Audit #2 (2026-09-26 morning) - done.** Stale-cache + old-firmware mismatch caused the Settings failures; version checks, visible save results, heap-safe settings save. Firmware 2026.09.26 (557,808 B). Installer (08:23) and Portable (08:51) rebuilt. ORDER FOR USER: flash firmware FIRST, then Upload GUI, then reload admin page.
- **Admin panel features + live-test fixes (2026-09-26) - done, awaiting hardware test.** Voucher print sheet, sales range/CSV + persistent daily totals, named Speed Profiles (dropdown in rates/subscribers, pushed to MikroTik), portal rate list + announcement, Coin Slot pin settings, named sound slots, open customer WiFi, status-page redirect loop fix. Voucher login confirmed working on real hardware. NEXT: rebuild exes, Upload GUI, flash, re-run NodeMCU wizard (open WiFi, blank password), coin acceptor wiring (need baseboard photo). See changelog.
- **First real test on hAP lite (2026-09-25 night) - in progress.** Found + fixed: hotspot blocked by RouterOS device-mode (fixed on router: device-mode update hotspot=yes scheduler=yes + power cycle), no hotspot html folder after Config (fixed on router: /ip hotspot reset-html zxheifi-hotspot, then GUI upload into `hotspot`), admin double-prompt/unauthorized, rate profile save, subscriber save without internet time. Firmware rebuilt 22:02 (550,352 B), installer rebuilt; portable pending (app was open). DONE 22:33: Test Connection device-mode check + Upload GUI Files button (FTP, reset-html, verify); both exes rebuilt. Not yet run against the real router.
- **Full audit + fixes (2026-09-25) — done, awaiting hardware test.** After Config hit the user's production hEX, audited the desktop app, firmware, GUI and .rsc scripts. Fixed: router target check + idempotent/removable config plan, defconf firewall, write-capable API user with generated password, walled garden + nodemcu.zxheifi.lan, sendOk() crash, session slot reuse, voucher/subscriber/log data loss (streaming persistence), unsynced-clock expiry, voucher rollback on failed provision, RouterOS !empty, elapsed-time charging, coin slot bound to a customer (coin_slot.h + GUI modal + top-up), Setup Mode FLASH window, LED polarity. .rsc files are now generated from the app plan (export_rsc.py). Tests: test_configurator ALL PASSED, regression 93/93, browser coin flow verified on the mock. Firmware rebuilt (549,936 B), exes rebuilt. Added Flash tab "Erase everything first" option (2026-09-25). Then fixed "stuck on Detecting": Bluetooth COM ports were listed/auto-probed first; now USB ports first with labels, no auto-scan, a Scan Device/Cancel Scan button, MAC picked up from flash output, probes killable, esptool worker dispatched before GUI imports. Tested from source (cancel on COM4, MAC on COM5 in 18s). Exes rebuilt after the PC restart (2026-09-25 19:19): esptool worker startup 0.8s installed / 10.5s portable (was 9.4s / 29s). NEXT: erase+flash COM5, run Config on the hAP lite (PC on static 192.168.88.10, WiFi off), upload GUI, Setup Wizard, phone voucher + coin test. See changelog.
- **In-app Guide tab + GitHub-readiness docs (2026-09-22) — done.** User
  wanted to try Configure MikroTik but said they don't know much about
  it yet, and separately asked for a full guide built into the app
  itself, plus a license/docs/Terms and Conditions since they plan to
  put this on GitHub. Confirmed: MIT license, "ZxheiFi" as the
  copyright holder (no personal name). Added a third "Guide" tab to the
  desktop app (`desktop-app/guide_content.py` + `GuideTab` in
  `main.py`) — Filipino/Taglish walkthrough covering the 3-step setup
  order, a full explanation of every Configure MikroTik checkbox/field
  (the thing the user said they didn't understand), the manual Winbox
  GUI-upload step, the sound system, and troubleshooting. Added root
  `LICENSE` (MIT) and `TERMS.md` (plain-language T&Cs, not legal
  advice), and fully rewrote the badly stale `README.md` (predated the
  desktop app entirely) to reflect the project as it actually is now.
  Rebuilt both packaged `.exe`s with the new tab. Nothing committed to
  git — left for the user. See changelog for full detail.
- **Firmware recompiled + reflashed with soundEnabled (2026-09-22) — done.**
  The on-disk `.bin` predated the sound-system's `AdminAPI::soundEnabled`
  field; recompiled clean via scratch PlatformIO (549,296 bytes, 52.2%
  flash), then reflashed to the real NodeMCU on COM5 using the app's own
  `FlashJob` class - hash verified, booted clean into Setup Mode. This
  also served as the real end-to-end proof that the esptool
  self-invoke fix (previous entry) works for actual flashing, not just
  MAC detection. See changelog for full detail.
- **Sound system + scrollable desktop-app tabs (2026-09-22) — done.**
  User asked for (1) sound/music on login.html/status.html (coin-insert
  cue, background music) and (2) the desktop app's Flash Firmware /
  Configure MikroTik tabs to be scrollable. Sound system is entirely
  file-driven (this project can't generate real audio) — drop `.mp3`
  files into `mikrotik/gui/sounds/` per that folder's README, admin
  toggles the whole feature on/off in Settings (`AdminAPI::soundEnabled`,
  default off), each visitor gets their own mute toggle. Wired into
  `tools/mock_server.py` and `docs/06-gui-customize.md`; all 73
  `tools/regression_test.py` assertions still pass. `desktop-app/main.py`'s
  `FlashTab`/`MikrotikTab` now build inside a `CTkScrollableFrame`;
  compiled and smoke-launched clean. Two-version packaging (portable +
  installer .exe) also done this session — see next entry.
- **Desktop app: auto-detect NodeMCU MAC/IP (2026-09-22) — done.** Flash
  Firmware tab's COM port dropdown now auto-runs `esptool read-mac`
  (works pre-WiFi-config too, talks to the bootloader directly) whenever
  the port changes, showing the result and feeding the MAC into the
  Configure MikroTik tab's NodeMCU MAC field automatically. Live-tested
  against the real NodeMCU on COM5. See changelog for full detail.
- **Fixed: packaged .exe couldn't run esptool at all (2026-09-22) — done.**
  Found by actually smoke-testing the built portable `.exe` against real
  hardware (not just `python main.py`): both flashing and MAC/IP
  detection called `[sys.executable, "-m", "esptool", ...]`, which only
  works when `sys.executable` is a real Python install - in a packaged
  `.exe` it's the app itself, so every flash/detect would have silently
  failed on any machine this actually shipped to. Fixed by having the
  app re-invoke itself with a `--esptool-worker` sentinel that runs
  `esptool._main()` in-process before any GUI code loads, plus
  `--collect-data esptool` in `build.py` (esptool's flasher stub
  payloads are package data, not code, so PyInstaller didn't bundle them
  either). Re-verified against the real NodeMCU on COM5 using the
  rebuilt packaged exe specifically. See changelog for full detail.
- **Desktop app two-version packaging (2026-09-22) — done.** New
  `desktop-app/build.py` builds both a portable single-file `.exe`
  (PyInstaller `--onefile`) and a proper Windows installer (PyInstaller
  `--onedir` wrapped by a new `desktop-app/installer.iss` via Inno
  Setup). Both built successfully and smoke-launched clean. See
  [docs/09-changelog.md](docs/09-changelog.md) for full detail.
- Note: a Unified Rate Profiles / coin-voucher rework / admin kick-block
  security fix / MikroTik port-roles pass also shipped this same day
  (2026-09-22, earlier) — see the changelog doc; this file's day-by-day
  narrative below hasn't been backfilled past 2026-09-04.

### 🗺️ Feature roadmap in progress
A large enhancement request (remote monitoring, sales reporting,
multi-admin, per-mode session limits, subscriptions, multi-coin,
e-load, content filtering, rebranding, i18n, and more) is being worked
through phase by phase — see [docs/10-feature-roadmap.md](docs/10-feature-roadmap.md)
for the full audit, phasing, and decisions log.
- **Phase A (MikroTik-side: content filtering, Gaming QoS mobile-game
  ports, WiFi coverage docs) — done**, see roadmap doc for details.
- **Phase B (per-tier Time/Data mode, manual Pause/Resume, Subscriptions)
  — done**, firmware recompiled clean (380,864 bytes) and verified
  end-to-end against `tools/mock_server.py`. See roadmap doc for details.
- **Phase C (multi-admin roles, activity logs, sales inventory,
  rebrandable portal) — done**, firmware recompiled clean (387,315
  bytes) and verified end-to-end against `tools/mock_server.py`.
  Multi-Currency and Multi-Language were dropped from scope per the
  user. Also fixed a pre-existing bug found along the way: several CSS
  classes used throughout every GUI page since Phase 0 (`.stats-grid`,
  `.stat-card`, `.table-container`, `.toggle-switch`) were never
  actually defined in `style.css`. See roadmap doc for details.
- **Phase D (coin acceptor: docs + defensive pulse hardening) — done**,
  firmware recompiled clean (391,744 bytes). Turned out to be a small
  phase — the existing single-GPIO design already fit a proportional-
  pulse multi-denomination acceptor. Found and fixed a more significant
  pre-existing bug while there: the coin-slot debounce never properly
  waited for a pulse burst to finish once running (self-referencing
  stale timestamp) — total credited amount was still correct, but the
  buzzer fired multiple times per coin and any burst-size check would
  have been ineffective. See roadmap doc for details.
- **Visual GUI review (2026-09-03)** — user asked to actually see the
  UI (all prior verification was JS/DOM-based, never a real
  screenshot). Found and fixed 3 real CSS bugs: `.login-form` missing
  `flex-direction:column` (fields/buttons overlapping horizontally),
  11 more classes with zero CSS definitions across all 3 pages, and
  `body` missing a base text color (black text on a near-black
  background). Screenshotted every page/tab after the fix to confirm.
  No firmware changes. See roadmap doc for full detail.
- Phase E not started yet.
- **Ad-hoc additions (2026-09-03, post-Phase D) — done.** A batch of
  4 follow-up requests, all implemented and verified: (1) coin slot
  pricing switched from fixed ₱10/20/30 tiers to a **proportional**
  peso→minutes/MB rate, admin-editable in Settings; (2) **voucher
  validity/expiration** (`voucherValidityDays`) plus **customizable
  Tier 1/2/3 price/time/data** via Settings (baked into each voucher at
  generation, non-retroactive); (3) **voucher ticket design
  customization** (`generator.py --brand-name/--accent-color/--footer-text`);
  (4) **mobile-responsive admin dashboard** (fixed a tier-pricing-row
  overflow and added a scroll-affordance chevron to the 7-tab nav, both
  found via real mobile-viewport screenshots). Firmware recompiled
  clean (389,643 bytes flash / 34,556 bytes RAM). See roadmap doc for
  full detail.
- **Bandwidth (Mbps) settings (2026-09-04) — done.** User asked to
  double-check voucher/coin validity (voucher: already working; coin:
  N/A, credit converts to a session instantly, no pending state to
  expire) and whether coin slot/voucher/PPPoE had bandwidth settings
  (they didn't — Mbps was 100% hardcoded in the `.rsc` files). Added
  admin-editable Download/Upload Mbps per tier, **shared across coin
  slot + voucher + PPPoE** (same tier = same MikroTik profile pair) and
  **live-pushed to the router** via a new `MikrotikAPI::
  setHotspotProfileRateLimit()`/`setPppProfileRateLimit()` on every
  Settings save — both user-confirmed choices. Firmware recompiled
  clean (391,071 bytes flash / 34,776 bytes RAM). See roadmap doc for
  full detail.
- **Bug sweep (2026-09-04) — done.** User asked to fix all bugs. Found
  and fixed two severe, previously-undetected bugs via a full re-read
  of the login/session/pppoe path: (1) **hotspot logins never actually
  released the client from MikroTik's walled garden** — `/api/login`
  only registered the account on the backend, nothing ever
  authenticated the browsing client itself against MikroTik's real
  `/login` handler, meaning real customers would see "Connected" while
  staying stuck behind the captive portal with no internet; fixed with
  a real form POST to `/login` after login and after resume; (2)
  **subscriber accounts got the wrong MikroTik password** — always
  defaulted to the session code instead of the subscriber's real
  password; fixed by threading the actual password through
  `PPPoEManager::provision()`. Also fixed 3 smaller issues in the same
  day's Mbps settings work (JSON buffer size, Mbps rounding precision,
  an inconsistent reachability gate). Firmware recompiled clean
  (391,287 bytes flash / 34,760 bytes RAM), verified end-to-end in a
  real browser. See roadmap doc for full detail.
- **Post-audit fixes (2026-09-04) — done.** User asked for another
  audit + prioritized suggestions, then to do what's possible without
  real hardware. Shipped: (1) **dynamic tier dropdown labels** across
  Vouchers/Subscriptions/Coin Slot tabs via a new `/api/admin/tier-info`
  endpoint open to both admin roles (unlike full Settings); (2)
  **login brute-force protection** — a shared per-IP failure table on
  `/api/login` and admin auth, 8 failures/minute triggers a 429
  lockout; (3) **`tools/regression_test.py`** — 57-assertion automated
  contract check against `mock_server.py`'s full `/api/*` surface,
  runs in seconds with no browser/hardware (caught and fixed a real
  in-process-threading deadlock in its own first draft). QR code
  generation for "Show QR" was deliberately deferred — a from-memory
  QR encoder risks a subtly broken implementation, worse than the
  honest text fallback already in place. Firmware recompiled clean
  (392,207 bytes flash / 35,000 bytes RAM). See roadmap doc for full
  detail.
- **Salted password hashing (2026-09-04) — done.** Continued the
  no-hardware-needed list. Admin/subscriber passwords now stored as
  `MD5(salt + password)` with a random per-account salt from
  `RANDOM_REG32` (ESP8266 hardware RNG), instead of plain unsalted MD5
  — defeats rainbow-table attacks against a stolen `admins.json`/
  `subscribers.json`. Zero-migration: empty salt (every existing
  account) reduces to the exact old unsalted hash, so nothing needs
  re-hashing and the default `admin`/`ADMIN_PASSWORD_HASH` account
  keeps working unchanged. Firmware recompiled clean (392,719 bytes
  flash / 35,016 bytes RAM); backward-compat math double-checked in
  Python, full regression suite re-run clean. See roadmap doc for full
  detail.
- **CRITICAL FIX (2026-09-04): cross-origin GUI ↔ NodeMCU
  communication.** User noticed voucher generation looked like it was
  saving to MikroTik instead of NodeMCU — the visible symptom of the
  single most important bug found in this project. Root cause: the GUI
  is served by MikroTik, the JSON API by the NodeMCU (two different
  devices/IPs), but every `/api/*` call in `script.js` used a plain
  relative path — which resolves against the *page's own origin*.
  In production this meant **every dynamic feature in the entire
  system (login, status, admin dashboard, voucher generation — all of
  it) would silently target the MikroTik router itself, which has no
  such routes, and never reach the NodeMCU at all.** Completely masked
  through the whole project so far because `tools/mock_server.py`
  serves both the GUI and the API from one combined origin. Fixed with
  `script.js`'s `apiBase()` (targets the NodeMCU's IP explicitly except
  in dev/testing) plus new CORS support in `gui_handler.h`. Verified
  with two separate `mock_server.py` instances on different ports
  (genuinely different browser origins) — real cross-origin fetches,
  including one triggering an actual CORS preflight, succeeded in a
  real browser. Firmware recompiled clean (393,019 bytes flash /
  35,128 bytes RAM), full regression suite re-run clean. See roadmap
  doc for full detail. **Now the #1 priority to confirm the moment
  real MikroTik/NodeMCU hardware is available** — without it nothing
  in the GUI would have worked in a real deployment.
- **Original-JuanFi audit + First-boot Setup Wizard (2026-09-04) —
  done.** User asked for a thorough re-check against the real original
  JuanFi repo (github.com/ivanalayan15/JuanFi), goal being that this
  project should be a strict superset of the original's real features.
  Fetched the actual repo (not just the existing, partly-inaccurate
  comparison doc) and found the biggest real gap: the original's
  first-boot setup wizard (no-reflash WiFi/MikroTik config), which this
  project lacked entirely. Built it via full architectural brainstorming
  (new subsystem: AP mode + captive portal the firmware runs INSTEAD of
  normal operation) — `firmware/network_config.h` (new,
  `/network.json` persistence, zero-migration), `firmware/setup_mode.h`
  (new, `SetupModeManager` — AP + `DNSServer` + one embedded form, only
  libraries already bundled with the esp8266 core), `MikrotikAPI`/
  `AdminAPI` gained runtime-configurable credentials, and **mDNS**
  (`juanfi-nodemcu.local`) replacing the hardcoded `NODEMCU_IP` in
  `script.js` — a hardcoded IP would only have worked paired with a
  manually-configured static DHCP lease, the same fragility that caused
  the cross-origin bug fixed earlier the same day. Also corrected
  `docs/00-github-comparison.md` (removed an outright wrong "Telegram
  preserved" claim) and flagged real remaining gaps: Telegram
  notifications, a physical Night Light relay, remote restart, real QR
  generation (original vendors a known library worth adopting), LAN/
  ESP32 hardware options, multi-vendo support. Firmware recompiled
  clean (423,991 bytes flash / 36,236 bytes RAM), full 57-check
  regression suite re-run clean. **User also confirmed a full GUI
  visual redesign as the next round of work** (not incremental polish),
  informed by the original's 5 template versions as design inspiration.
  See roadmap doc for full detail. **Not live-tested** — highest
  priority to confirm on real hardware alongside the walled-garden and
  cross-origin fixes.
- **Category C follow-through (2026-09-04) — done.** User asked to
  finish the well-scoped, no-decision-needed items from the audit
  before starting the GUI redesign, with proper planning/review first.
  (1) **Random MAC reconnect fix** — investigated first per the user's
  request, confirmed this project WAS genuinely affected (a
  MAC-changed reconnect would hit the single-use voucher rejection AND
  get blocked by a stale `/ip/hotspot/active` entry under
  `shared-users=1`); fixed with a new `kickActiveHotspotUser()`
  (deliberately not folded into the path Extend also uses) plus a
  reconnect branch in `handleLogin()`. (2) **Real QR code generation**
  — vendored the original JuanFi's own `qrcode.min.js`
  (davidshimjs/qrcodejs, MIT), scanned for anything suspicious first;
  verified with genuine round-trip decoding (independent jsQR decoder
  confirmed the generated QR exactly matches the session code) rather
  than just eyeballing it. (3) **Telegram notifications** — real gap
  closed; NodeMCU-side HTTPS to the Bot API (`firmware/telegram.h`),
  messages queued and sent one at a time from the main loop so a 1-3s
  TLS handshake never delays a customer's login response, fires on
  genuine new sales only. Firmware recompiled clean at each step (final:
  532,759 bytes flash / 37,052 bytes RAM — the BearSSL/TLS stack for
  Telegram is the single largest flash addition this project has made,
  pushing past the old 500KB budget, which was accordingly raised to
  900KB to reflect the real ~1MB partition ceiling; still only ~51%
  used). Full 60-check regression suite clean throughout. See roadmap
  doc for full detail. **Not live-tested**: random-MAC fix needs a real
  MikroTik router; QR generation verified in software only (no physical
  phone camera); Telegram not tested against a real bot token.
- **Full GUI visual redesign ("Coin Meter" theme) — done.** User asked
  to move on to the previously-approved full redesign. Researched the
  original JuanFi's actual latest template (not a guess) and found a
  consistent "coin-op utility meter" vocabulary across every version —
  used that as the grounding subject instead of a generic dark-SaaS
  default. New brass/peso/alert palette (brass rebrandable, peso-green/
  alert-red fixed semantic colors), system-fonts-only (a hard
  requirement — login/status pages are shown before the customer has
  any internet access at all), monospace/tabular-nums treatment for
  every "meter readout" number, and a coin-slot-styled code input as
  the one signature element. Structural HTML/class names unchanged —
  a CSS-level redesign, so no `script.js` DOM logic needed touching
  beyond fixing 2 stray hardcoded colors. Caught and fixed a real
  default-brand-color mismatch during testing (several files still
  defaulted to the old teal, invisible under `applyBranding()`'s
  override). Verified with real browser screenshots at both desktop
  and mobile widths across all 3 pages. Firmware recompiled clean
  (unchanged from the Telegram entry above - no firmware code touched
  this round), full regression suite re-run clean. See roadmap doc for
  full detail. **Not live-tested** on real device/browser diversity
  beyond this session's Chromium-based Browser pane.
- **Real RouterOS API testing rig (CHR + QEMU) + a genuine `run()` bug
  found and fixed — done.** User asked about using an emulator; the
  RouterOS binary API client (`firmware/mikrotik_api.h`) had never
  been tested against a real router (`mock_server.py` only mocks the
  NodeMCU's own JSON contract, not the RouterOS API protocol). Set up
  MikroTik CHR under QEMU inside WSL2 with no admin/root access at all
  (`apt-get download` + manual `dpkg-deb -x` extraction for every
  package and transitive library QEMU needed, software-only TCG boot).
  A Python client mirroring `mikrotik_api.h`'s exact wire protocol
  found a real bug: `run()` returned immediately on a RouterOS `!trap`
  reply without reading the trailing `!done` RouterOS always sends
  after it, desyncing every subsequent reply on that connection by one
  sentence after the first router-side rejection — confirmed on the
  wire, then reproduced (`removeHotspotUser()` reporting success while
  the object stayed, later queries returning unrelated rows). Fixed by
  looping through `!trap` until the real `!done` arrives instead of
  returning early. Firmware recompiled clean (532,775 bytes flash /
  37,052 bytes RAM), 19/19 real-RouterOS checks pass post-fix, full
  60-check regression suite still clean. See roadmap doc and
  [09-changelog.md](docs/09-changelog.md) for full detail. **Not
  live-tested** on real hardware — this closes the protocol-
  correctness gap, not the real-ESP8266+real-phone gap.
- **Rebrand to ZxheiFi + logo re-theme + ZxheiFi Setup Companion desktop
  app — done.** User asked to rename the project to their own brand
  using their own logo, and to build a firmware flasher + checkbox-
  driven MikroTik configurator. Confirmed the codebase is theirs to
  rebrand (per the github-comparison audit, not a derivative of
  upstream JuanFi's source, aside from the already-attributed
  `qrcode.min.js`). **Real hardware milestone**: the NodeMCU arrived on
  COM5 this session — first-ever real flash + boot + WiFi-scan-confirmed
  Setup Mode AP, before and after the rebrand. Re-themed
  `mikrotik/gui/style.css` with colors sampled directly from the logo
  PNG (Pillow, not eyeballed), processed the logo's white background
  into transparency via border-flood-fill (preserving its own white
  wordmark text), and found/fixed 4 buttons/gradients still hardcoded
  to the old brass theme. Swept ~40 files renaming JuanFi→ZxheiFi
  (SSIDs, mDNS hostname, API username, docs), leaving historical
  changelog/roadmap entries and the real upstream-project comparison
  doc untouched. Built `desktop-app/` (Python + customtkinter): a Flash
  Firmware tab wrapping `esptool` + a serial monitor, and a Configure
  MikroTik tab executing the `.rsc` files' own command sequences over
  the real RouterOS API (reusing the validated client above). Found 4
  more real RouterOS parameter bugs this way (never caught before,
  since nothing had run these commands against real RouterOS either):
  `cookie-max-age`→`http-cookie-lifetime`, removed the nonexistent
  `use-vj-compression`, `fq_codel`→`fq-codel` (queue kind + all
  sub-params), and `idle-timeout`/`keepalive-timeout` moved from the
  hotspot profile to the hotspot server command — fixed in both the app
  and the two `.rsc` files. Verified: firmware recompiled clean
  (532,759 bytes flash / 37,036 bytes RAM), 60-check regression suite
  clean, **reflashed the real NodeMCU using the new flasher tool
  itself** (confirmed `ZXHEIFI NODEMCU FIRMWARE` boot banner and
  `ZxheiFi-Setup` AP broadcasting on real hardware), 43/43
  interface-valid dry-run steps against a freshly reset CHR router (the
  remaining 9 failures are 100% due to this being a single-NIC,
  no-wireless test VM — not command defects). See
  [09-changelog.md](docs/09-changelog.md) for full detail. **Not yet
  done**: packaging the desktop app as a distributable `.exe`; the
  physical hAP lite's actual WiFi/PoE behavior still needs the user's
  own hardware connected via LAN + Winbox.
- **Unified Rate Profiles, coin/voucher rework, admin kick/block
  security fix, MikroTik port roles — done.** User asked for one
  admin-defined pricing table (peso→minutes→validity, in minutes not
  hours/days) shared by BOTH the coin slot and vouchers, plus
  export/import, real kick/block, and per-port MikroTik roles. Replaced
  the old fixed Tier1/2/3 + separate linear coin rate with
  `AdminAPI::rateProfiles` (auto-migrates existing devices from the old
  constants). Coin crediting is now additive per exact denomination
  (a ₱5 coin can be worth more than 5x a ₱1 coin) instead of one linear
  rate. "Validity" reuses the existing pause-then-forfeit mechanism
  (`session.h`'s `MAX_PAUSE_MINUTES`), just made per-profile. **Found
  a real, exploitable bug while building Kick**: the self-service
  disconnect/pause/resume endpoints had zero admin/ownership check, and
  the dashboard's existing Kick button called them directly — anyone
  who merely knew a session id (a voucher code) could kick or pause
  ANY other customer's session, unauthenticated. Fixed with genuinely
  separate `requireAdmin()`-gated `/api/admin/kick` and a new
  `/api/admin/block` (persistent RouterOS ip-binding block, holds
  across a NodeMCU reboot). Desktop app's Configure MikroTik tab
  gained per-port roles (WAN/Hotspot/PPPoE/LAN/Unused), defaulting to
  the exact old hardcoded assignment. `vouchers/generator.py` now reads
  the same `rate_profiles.json` the dashboard exports, closing the
  "two independently-maintained price lists" gap. Verified:
  regression suite grew 60→73 checks, all clean; firmware recompiled
  clean (540,784 bytes flash / 37,364 bytes RAM); full browser
  click-through (added a profile, generated + redeemed a voucher off
  it, confirmed Kick hits the new endpoint via network-request
  inspection). See [09-changelog.md](docs/09-changelog.md) for full
  detail. **Not yet done**: the NodeMCU got disconnected from COM5
  mid-session (likely unplugged) so this update hasn't been reflashed
  to real hardware yet, and the port-roles feature hasn't been
  dry-run against the CHR rig.

### ✅ COMPLETED
1. [x] Project structure & directory setup
2. [x] docs/00-github-comparison.md — Original vs Enhanced comparison
3. [x] docs/01-overview.md — Project overview
4. [x] docs/02-hardware-setup.md — Hardware checklist (fixed: NodeMCU has no
   Ethernet port; it joins the MikroTik AP over WiFi, wiring diagram corrected)
5. [x] mikrotik/hAP_lite_full_config.rsc — Complete configuration
6. [x] mikrotik/hEX_full_config.rsc — Complete configuration
7. [x] mikrotik/gaming_qos_fq_codel.rsc — Gaming QoS module
8. [x] mikrotik/pppoe_server_setup.rsc — PPPoE server module
9. [x] mikrotik/import_guide.md — Import instructions
10. [x] firmware/config.h — Configuration constants
11. [x] firmware/nodemcu_firmware.ino — Rewritten: was including 4 nonexistent
    `.cpp` modules and calling 8 undefined functions, so it could not compile.
    Now includes header-only modules (fixes an Arduino build-system bug where
    `#include`-ing another `.cpp` in the sketch folder causes duplicate-symbol
    linker errors) and implements every function it calls.
12. [x] firmware/mikrotik_api.h — RouterOS API client (binary protocol,
    port 8728) — new, did not exist before
13. [x] firmware/session.h — Active session tracking, Time+Data combo,
    Idle Auto-Pause, SPIFFS recovery. Replaces the old `session.cpp`,
    which was pseudocode/markdown saved with a `.cpp` extension and
    contained an invalid 18-bit `uint16_t` bitfield — not valid C++.
14. [x] firmware/qos.h — Tier → MikroTik profile mapping (matches the
    `hs-*`/`pppoe-*` profile names in the `.rsc` scripts) + Night Promo
    hour check (NTP-based) — new
15. [x] firmware/pppoe.h — Dual mode (Hotspot/PPPoE) provisioning — new
16. [x] firmware/admin_api.h — Voucher store, CSV import, on-device
    voucher generation, sales counters, settings — new
17. [x] firmware/gui_handler.h — JSON API consumed by the GUI's
    `script.js` (login/status/extend/disconnect + admin endpoints) — new
18. [x] mikrotik/gui/login.html, status.html, admin.html — fixed a
    malformed `<button>` tag in admin.html and a CSS bug that hid the
    Overview tab on load
19. [x] mikrotik/gui/script.js — was truncated mid-function and missing
    every handler the 3 HTML pages call (`scanQR`, `extendSession`,
    `disconnect`, `openTab`, `generateVouchers`, `saveSettings`, etc.).
    Rewritten complete; QR scanning uses the browser's native
    `BarcodeDetector` API (no unverified hand-rolled decoder)
20. [x] vouchers/generator.py — voucher code + CSV + printable thermal
    HTML ticket generator with embedded QR codes — new, tested (QR
    payloads decoded and confirmed to match the printed code)
21. [x] vouchers/print_template.html — 58mm thermal receipt layout — new

### ⚠️ Known gaps (honest, not hidden)
- [x] Firmware **compiles successfully** — verified with a real
  PlatformIO build for the nodemcuv2 target: 370,323 / 1,044,464 bytes
  flash (35.5%, well under the 500KB budget), 32,700 / 81,920 bytes RAM
  (39.9%), zero errors. There are ~10 harmless warnings (SPIFFS is
  deprecated in favor of LittleFS upstream but still fully functional;
  one cosmetic `snprintf` truncation false-positive on the date-stamp
  buffer) — none are correctness issues.
- [ ] It has **not** been flash-tested on real hardware — no physical
  NodeMCU/MikroTik was available in this session to verify RouterOS API
  behavior end-to-end.
- [x] The GUI ↔ JSON API contract itself **was** verified end-to-end
  against a mock backend (`tools/mock_server.py`, new — implements the
  same `/api/*` routes as `firmware/gui_handler.h`): real browser
  clicks through login → live-polled status countdown → extend (correct
  combined time/data math) → disconnect (404 after) → admin dashboard
  (auth, overview stats, voucher generation) all worked. One real bug
  was found and fixed in the process — the mock's `HTTPServer` needed
  to be `ThreadingHTTPServer` to handle concurrent browser requests; the
  actual firmware doesn't have this issue since ESP8266WebServer runs
  in the single-threaded Arduino loop the same way the browser expects.
- [ ] "Show QR" on status.html displays the session code as text, not an
  actual scannable QR matrix — no offline QR-encoding JS library is
  vendored yet. Scanning a *printed* voucher (generator.py's output) via
  BarcodeDetector works; generating one *in the browser* does not yet.
- [ ] docs/03 through docs/09 are still not written (this file previously
  claimed they were "COMPLETED" — that was inaccurate; they never existed
  in the repo).
- [x] `firmware/juanfi_enhanced_firmware.bin` is now a real compiled
  binary (374,480 bytes, PlatformIO/nodemcuv2, built 2026-08-25) — built
  from `firmware/config.h` **with its default/placeholder values**
  (`WIFI_PASSWORD="yourwifipassword123"`, `MIKROTIK_API_PASS="changeme123"`,
  admin password `"admin"`, etc). Anyone deploying this for real should
  edit `config.h` per `docs/05-nodemcu-flash.md` step 1 and rebuild —
  do not flash the placeholder credentials to a production kiosk.

### 📋 ARCHITECTURE (unchanged)
- GUI files served by MikroTik Hotspot (no size limit) — kept under
  `mikrotik/gui/` since that's what gets uploaded to the router's
  `flash/hotspot` directory
- NodeMCU firmware: logic + JSON API only (no HTML/CSS/JS)
- User flow: User → MikroTik Hotspot GUI → NodeMCU JSON API → RouterOS API
