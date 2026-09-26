# 🗺️ Feature Roadmap — Requested Enhancements

Tracking doc for the feature request discussed starting 2026-09-03.
Updated as decisions are made — this is the running record the user
asked for, not a finished spec.

## Status legend
✅ already have it | ⚠️ partial / needs clarification | ❌ doesn't exist yet | ➖ dropped from scope

## 1. Management at Monitoring
| Feature | Status | Notes |
|---|---|---|
| Remote Online Management | ❌ | Admin dashboard is LAN-only today. Needs an architecture decision: port-forward+DDNS, a cloud relay service, or MikroTik Cloud. Biggest fork in the whole roadmap. Still the only item in this section not done. |
| Real-time Sales Inventory (daily/weekly/monthly/yearly, filterable by payment type) | ✅ | **Phase C.** 90-day rolling daily summaries split by coin/voucher/subscription, Today/Week/Month quick totals — Sales tab. |
| Activity & Device Logs | ✅ | **Phase C.** Rolling 300-event log (coin, voucher/subscriber logins, admin actions, system events) — Logs tab. |
| Multi-Admin with access levels | ✅ | **Phase C.** Super/staff roles, dynamic accounts via dashboard, last-super-admin lockout protection — Admins tab. |

## 2. Network at Internet Control
| Feature | Status | Notes |
|---|---|---|
| Bandwidth Limiter per user | ✅ | MikroTik hotspot/PPP profiles already rate-limit per tier (`hs-default/gaming/high`, `pppoe-*`). |
| Anti-Lag / Traffic Shaper | ✅ | Gaming QoS in `mikrotik/gaming_qos_fq_codel.rsc`, **Phase A** added a wide UDP range for Mobile Legends/COD Mobile (community-sourced, since neither publishes fixed ports). |
| Multi-LAN + PPPoE (multiple independent interfaces, each with own DHCP/portal) | ⚠️ | Dual mode (Hotspot **and** PPPoE together) already works. True multi-zone (e.g. separate branded portals per building/floor) does not exist — needs clarification on what's actually wanted. Not yet scoped into a phase. |
| Extendable WiFi Coverage | ✅ | **Phase A.** Repeater/CPE guidance added to `docs/03` (hAP Lite) and `docs/04` (hEX). |

## 3. Session Management
| Feature | Status | Notes |
|---|---|---|
| Time-only or Data-only mode (not always combo) | ✅ | **Phase B.** Admin sets per-tier in Settings; unlimited sentinel on the "off" budget, bandwidth speed still capped by the MikroTik profile. |
| Pause / Resume (user-initiated) | ✅ | **Phase B.** Actually deprovisions on MikroTik, 2-hour max pause before auto-drop. |
| Subscription system (postpaid-style, for boarding houses etc.) | ✅ | **Phase B.** Admin-managed billing, username/password identity, unlimited while active, cuts off immediately on expiry. |

## 4. Hardware at Payment
| Feature | Status | Notes |
|---|---|---|
| Multi-Coin Acceptor (₱1/5/10/20) | ✅ | **Phase D.** User confirmed this means a single proportional-pulse acceptor (pulse count = peso value), which the existing single-GPIO design already fits — no new hardware/GPIO needed. See `docs/11-coin-acceptor-wiring.md`. |
| Built-in E-Loading Integration | ❌ | Needs a real e-load provider account/API — as much a business/procurement decision as an engineering one. Phase E. |
| Content Filtering | ✅ | **Phase A.** DNS-based (CleanBrowsing Family Filter), always-on, MikroTik-side. |
| Rebrandable Portal | ✅ | **Phase C.** Business name + accent color, no-code via Settings tab, applied live. Logo stays manual-edit by design (storage risk). |
| Multi-Currency | ➖ dropped | Coin acceptor hardware is currency-specific; low value for a PH-only deployment. Revisit only if actually expanding abroad. |
| Multi-Language (EN/ES/MS/ID) | ➖ dropped | User only wanted the existing English default at this time. |

## Proposed phasing
1. **Phase A** — MikroTik-side, low risk: Content Filtering, Gaming QoS port audit, WiFi coverage docs
2. **Phase B** — Session logic: Time/Data mode toggle, manual Pause/Resume, Subscription system
3. **Phase C** — Admin/ops + modernized GUI (largest single phase): multi-admin roles, persistent logs, filterable sales reports, rebrandable portal, multi-language
4. **Phase D** — Hardware: multi-coin-slot support
5. **Phase E** — Biggest architectural fork, external dependencies: Remote/cloud management, E-load integration

## Decisions log
*(filled in as we agree on things)*
- 2026-09-03: Roadmap drafted, phasing proposed, awaiting priority confirmation from user.
- 2026-09-03: User picked **Phase A first** (Content Filtering, Gaming QoS
  port audit, WiFi coverage docs). Now scoping Phase A in detail.
- 2026-09-03: Content filtering = DNS-based (CleanBrowsing Family Filter),
  **always-on** (no admin toggle, pure `.rsc` change — user's choice over
  a togglable version to keep it simple and avoid accidental disable).
- 2026-09-03: Gaming QoS for Mobile Legends/COD Mobile = widen the UDP
  priority port range (user's choice over packet-size heuristics, since
  ML/COD Mobile don't publish fixed ports).
- 2026-09-03: **Phase A implemented and approved.** Changes: new
  `mikrotik/content_filter.rsc` (standalone) + inlined into both
  `.rsc` configs (new numbered section, DNS switched to CleanBrowsing
  185.228.168.168/169.168, DNS force-redirect, DoT block); new mangle
  rule pair in `gaming_qos_fq_codel.rsc` + both configs for ML/COD
  Mobile's approximate UDP range; WiFi coverage extension guidance
  added to `docs/03` (repeater/CPE for hAP Lite) and `docs/04`
  (multi-AP/CPE for hEX); `docs/07-features-guide.md` updated.
  **Not live-tested** — no real MikroTik router available in this
  session; verified by careful syntax review against the
  already-proven patterns in the existing configs.

## Phase A — done (see Decisions log above for what shipped)

- 2026-09-03: Phase B done, user picked **Phase C next** (Admin/ops +
  modernized GUI). **Multi-Currency dropped from scope** (coin acceptor
  hardware is currency-specific, low real-world value for a PH-only
  deployment — revisit only if actually expanding abroad). **Multi-Language
  also dropped for now** — user only selected the existing English
  default, meaning no other language is needed at this time. Phase C
  scope is now: Multi-admin roles, persistent Activity/Device Logs,
  filterable Sales Inventory, Rebrandable portal (admin UI).

- 2026-09-03: User picked **Phase B next** (Time/Data mode toggle,
  manual Pause/Resume, Subscription system). Scoping item by item —
  starting with Time/Data mode toggle.
- 2026-09-03: Time/Data mode = admin sets it **per-tier** (not per-
  purchase); time-only mode gives truly unlimited data, no safety-net
  cap (bandwidth Mbps is still capped by the MikroTik profile either way).
- 2026-09-03: Pause = **actually cuts MikroTik access** (not just stops
  the timer while still connected), with a **2-hour max pause** before
  auto-expiring an abandoned session.
- 2026-09-03: Subscriptions = **admin-managed billing** (no online
  payment gateway), identity is **username+password** (not MAC-bound,
  works on any device), **unlimited time+data** while active (no
  monthly data cap), and **cuts off immediately** on expiry (no grace
  period).
- 2026-09-03: **Phase B implemented and approved.** Changes:
  `config.h` (UNLIMITED_SECONDS/BYTES sentinels, MAX_PAUSE_MINUTES,
  MAX_SUBSCRIBERS); `session.h` (paused/pausedAtMs/isSubscriber fields,
  pauseSession/resumeSession/isPausedTooLong); `admin_api.h` (tierMode1-3
  settings + tierModeFor(), full Subscriber struct + CRUD);
  `gui_handler.h` (subscriber-aware login, /api/pause, /api/resume,
  4 new /api/admin/subscribers* routes); `mikrotik_api.h`
  (addHotspotUser omits limit-uptime/limit-bytes-total entirely for
  unlimited sessions, rather than sending a huge number - the correct
  way to tell RouterOS "no limit"); `.ino` (checkSubscriptions(),
  paused/subscriber-aware queryActiveSessions()); GUI (`status.html`
  Pause/Resume button, `admin.html` Subscriptions tab + per-tier mode
  selects in Settings, `login.html` password field now actually gets
  sent, `script.js` throughout).
  **Verified**: full firmware **compiled successfully** with PlatformIO
  (380,864 bytes flash, still comfortably under the 500KB budget). One
  real bug was caught by recompiling and fixed: `collectHeaders()` on
  this ESP8266WebServer version is a variadic template (each header
  name its own argument), not the `(array, count)` form used in Phase 0
  - the earlier "successful" compile apparently used a different cached
  package version that accepted the old form; recompiling after every
  change (rather than trusting an earlier verified build) is what
  caught this. Also tested end-to-end against `tools/mock_server.py`
  (updated to mirror the same Phase B contract): subscriber login,
  pause/resume, time-only mode showing "Unlimited" data correctly on
  the status page, and the full admin Subscriptions tab (add/list/
  toggle, disabled subscriber correctly rejected on login) all worked
  in a real browser.
  **Not live-tested** — no real MikroTik/NodeMCU hardware available in
  this session.
- 2026-09-03: Phase B done, user picked **Phase C next** (Admin/ops +
  modernized GUI). Multi-Currency and Multi-Language dropped from scope
  (see table notes above). Phase C scoping: 2 roles (super/staff),
  accounts managed dynamically via dashboard; rolling ~300-event
  activity log covering coin/logins/admin actions/system events;
  90-day rolling daily sales summaries split by payment method, shown
  as a table + Today/Week/Month quick totals; rebranding limited to
  business name + accent color (logo stays manual-edit, storage risk).
- 2026-09-03: **Phase C implemented and approved.** Changes:
  `config.h` (MAX_ADMIN_ACCOUNTS, MAX_ACTIVITY_LOG_ENTRIES,
  MAX_SALES_HISTORY_DAYS); `admin_api.h` (`AdminAccount`/
  `ActivityLogEntry`/`DailySalesEntry` structs + full CRUD, revenue
  split into coin/voucher/subscription counters, `brandName`/
  `brandColor` settings, first-boot seeds a default super admin from
  the existing `ADMIN_PASSWORD_HASH` so upgrades don't lock anyone
  out); `gui_handler.h` (`requireAdmin(requireSuper)` now checks
  username+password and returns the account, public `/api/branding`,
  9 new/changed admin routes); `.ino` (coin revenue now tagged
  separately, `mikrotik_lost`/`restored`/`system_boot` log events,
  periodic activity-log flush only when dirty - not on every event, to
  spare the flash). GUI: `admin.html` grew from 4 tabs to 7 (Sales,
  Logs, Admins added), widened to 900px (`.container-wide` - the
  login/status pages stay phone-width on purpose), price field added
  to subscriber add/renew; `style.css` gained a `--accent`/
  `--accent-dark` CSS variable pair driving a no-code rebrand, **plus
  a fix for a pre-existing bug found while extending it**: `.stats-grid`/
  `.stat-card`/`.stat-value`/`.table-container`/`.toggle-switch` were
  referenced throughout every HTML page since Phase 0 but never
  defined - the Overview stat cards, all data tables, and the Night
  Promo toggle have been unstyled this whole time; `script.js` gained
  role-based tab hiding (`applyRoleVisibility()`), `applyBranding()`,
  and handlers for every new tab.
  **Verified**: firmware compiled successfully (387,315 bytes flash,
  still well under budget), zero new warnings beyond the known SPIFFS-
  deprecation ones. Tested end-to-end in a real browser against an
  updated `tools/mock_server.py`: super vs staff tab visibility, the
  last-super-admin lockout, sales breakdown/totals, activity log
  entries, and a live business-name + accent-color rebrand that
  persisted across a page navigation.
  **Not live-tested** — no real MikroTik/NodeMCU hardware available in
  this session.

- 2026-09-03: User said to proceed. Moving to **Phase D** next (natural
  order after C) — multi-coin-slot hardware support. Scoping the
  hardware questions first since this phase can't be designed blind.
- 2026-09-03: Confirmed the actual need is a **single multi-denomination
  acceptor** (not multiple independent physical slots), and that it's a
  **proportional-pulse** type (pulse count = peso value) rather than a
  separate-wire-per-denomination type. This meant the existing
  single-GPIO design already fit architecturally — no new hardware or
  session logic needed, just documentation + defensive hardening.
- 2026-09-03: **Phase D implemented and approved.** While reviewing the
  debounce logic to add the planned burst-size check, found a more
  significant **pre-existing bug**: `processCoinSlot()`'s debounce
  compared `millis()` against a timestamp (`coinLastPulseMs`) that only
  got updated *inside* the "still waiting" branch itself — so once a
  poll cycle fell through to processing (which it would on the very
  first pulse of nearly every coin, since the timestamp was stale from
  long before), it never had a fresh reference to compare against
  again, and kept processing on almost every subsequent poll instead of
  properly waiting for the whole pulse burst to finish. The credited
  peso amount still ended up correct (partial batches still summed
  correctly), but the buzzer would fire multiple times per coin instead
  of once, and my new max-plausible-pulses check would have evaluated
  tiny fragments instead of the real burst size, making it far less
  effective. Fixed by having the ISR itself stamp a `lastPulseMs` on
  every accepted pulse, so the debounce has a live reference regardless
  of polling timing.
  Changes: `config.h` (`MIN_PULSE_INTERVAL_US`, `MAX_PLAUSIBLE_PULSES`);
  `.ino` (ISR now stamps `lastPulseMs`/filters sub-5ms noise pulses,
  `processCoinSlot()`'s debounce fixed + rejects/logs implausible
  bursts as `coin_anomaly`); new `docs/11-coin-acceptor-wiring.md`
  (wiring table, voltage/opto-isolation warning, how to program a
  typical PH piso-wifi acceptor module for proportional pulses,
  testing steps); `docs/07-features-guide.md` and `docs/01-overview.md`
  updated.
  **Verified**: firmware compiled successfully (391,744 bytes,
  well under budget), no new warnings.
  **Not live-tested** — no real coin acceptor or NodeMCU hardware
  available in this session; the debounce fix was verified by careful
  logic tracing (walked through the pulse-burst timeline by hand)
  rather than an actual coin drop.

## 2026-09-03 — Visual GUI review (user asked to see the actual UI)
Four phases of GUI work had only ever been verified through JS/DOM
checks (`fetch()` calls, `getComputedStyle()`, reading `innerHTML`) —
never actually looked at as a human would see it. The user asked to
see it, so a real screenshot pass was done across all 3 pages and all
7 admin tabs. It found **three real, previously-undetected CSS bugs**
that no functional test would have caught:

1. `.login-form` had no `flex-direction: column` — `script.js` sets
   `display:flex` to show/hide the login forms, but without that
   property the username field, password field, QR button, and
   Connect button all laid out **horizontally** in a ~420px card,
   severely overlapping. Fixed with one property.
2. `.mode-toggle`, `.mode-btn`, `.qr-scan`, `.pppoe-info`,
   `.status-card`, `.status-item`, `.label`, `.value`, `.controls`,
   `.qr-text`, `.voucher-list` — eleven more classes referenced across
   `login.html`/`status.html`/`admin.html` with **zero CSS definitions**
   at all, on top of the five already found and fixed in Phase C
   (`.stats-grid` etc). All eleven now have real styles matching the
   existing glass-morphism theme.
3. `body` never set a base text `color`, so any element without an
   explicit color override (table cells, several headings) fell back
   to the browser default — **black text on a near-black background**,
   essentially invisible. One line (`color: #e8e8ec` on `body`) fixed
   every instance of it at once, since CSS color inherits by default.

A systematic audit (extract every `class="..."` used across the 3 HTML
files, grep each one against `style.css`) confirmed zero classes remain
undefined after the fix. Re-verified visually: login (both modes),
status (including the QR share modal), and all 7 admin tabs (Overview,
Vouchers, Active Users, Subscriptions, Sales, Logs, Admins, Settings)
all render correctly now. No firmware changes in this pass — CSS/GUI
only, so no recompile needed.

**Lesson for future phases**: a functional/DOM test suite (what this
project has relied on throughout, via `tools/mock_server.py`) cannot
catch "the CSS for this class doesn't exist" or "black text on black
background" — those only show up in an actual screenshot. Worth an
occasional visual pass, not just after GUI-heavy phases.

## 2026-09-03 — Ad-hoc additions (post-Phase D, user follow-up requests)
After the Visual GUI review, the user asked a rapid series of follow-up
questions/requests that didn't map to a single roadmap item — each
answered/implemented in turn rather than re-scoped into a new lettered
phase:

1. **Coin slot: proportional pricing, not fixed tiers.** User asked
   whether a ₱5 or ₱10 drop had a defined time value — it didn't; the
   old design only granted anything once credit reached a full tier
   price. User chose (via AskUserQuestion) to **replace** fixed-tier
   coin pricing with a proportional peso→minutes/MB rate, admin-
   editable in Settings (`coinMinutesPerPeso`, `coinMbPerPeso`,
   `coinQosTier` — picks the MikroTik bandwidth profile, since a
   proportional amount has no natural tier). `processCoinSlot()`
   rewritten accordingly. See [11-coin-acceptor-wiring.md](11-coin-acceptor-wiring.md).
2. **Voucher validity/expiration + customizable tier pricing.** User
   asked for voucher expiration ("lagyan mo rin ng validity") and for
   voucher Tier 1/2/3 price/time/data to be admin-editable via Settings
   instead of hardcoded, matching the same customization the coin slot
   just got. Added `voucherValidityDays` (0 = never expires) and
   per-tier `tier1-3Price/TimeMin/DataMb` fields to `AdminAPI`,
   surfaced in `admin.html`'s new "Voucher Pricing" Settings section.
   Values are **baked into each `VoucherRecord`/CSV row at generation
   time** — matching the project's established non-retroactive design
   principle (Settings changes never alter already-issued vouchers).
3. **Architecture clarification + voucher ticket design customization.**
   User asked which side (NodeMCU vs. MikroTik) owns voucher codes —
   answered: NodeMCU owns everything (`AdminAPI`'s voucher pool,
   generation, redemption); MikroTik only executes the resulting
   hotspot-user commands. User then asked for the *printed ticket's
   visual design* to also be customizable, ideally from Settings. Asked
   where this should live (NodeMCU Settings, syncing to `generator.py`,
   vs. `generator.py`'s own CLI flags); user picked **`generator.py`
   directly** (simpler, avoids inventing a NodeMCU↔Python sync
   mechanism for something used offline). Added `--brand-name`,
   `--accent-color`, `--footer-text` flags; `print_template.html` gained
   an `{{ACCENT_COLOR}}` token (border colors stay `#000` for thermal-
   print compatibility).
4. **Mobile-responsive admin settings.** User asked that admin Settings
   (and the dashboard generally) work well on a phone. Fixed two real
   mobile layout bugs found via actual `resize_window` mobile-viewport
   screenshots: the new 3-input tier-pricing rows overflowed their card
   at 375px width (fixed with a `.tier-price-row` class + mobile
   `flex-direction:column` media query), and the 7-tab admin nav bar had
   no visible affordance that more tabs existed off-screen (an initial
   `mask-image` fade attempt was confirmed applied but visually
   ineffective against the dark theme; replaced with a `.tabs-wrap::after`
   accent-colored `›` chevron that doesn't scroll away with the tabs).

**Verified**: firmware compiled successfully (389,643 bytes flash /
34,556 bytes RAM, `.bin` 393,792 bytes — only pre-existing SPIFFS-
deprecation + a cosmetic snprintf-truncation warning, no errors).
Functional pass against an updated `tools/mock_server.py` confirmed:
expired-voucher login/extend rejection, live-Settings voucher
generation (new vouchers use current tier pricing/validity), and
non-retroactivity (previously generated vouchers keep their original
baked-in price/time/data after a Settings change). `generator.py`
re-run with all four new flags plus `--validity-days 30` — QR codes
still decode correctly (pyzbar) and every customization string appears
in the rendered ticket/CSV. Mobile layouts re-checked visually after
each CSS fix.
**Not live-tested** — no real MikroTik/NodeMCU/coin-acceptor hardware
available in this session.

## 2026-09-04 — Bandwidth (Mbps) settings audit + addition
User asked to double-check whether coin slot and voucher Settings had
validity/expiration, and separately whether coin slot, voucher, and
PPPoE had Mbps (bandwidth) settings. Audit findings:
- **Voucher validity**: confirmed already working (`voucherValidityDays`,
  see the 2026-09-03 ad-hoc entry above) — no gap.
- **Coin slot validity**: doesn't apply the same way — a coin credit
  converts to an active session *instantly* (`processCoinSlot()`
  provisions as soon as `coinCreditPhp > 0`, no stored/pending state),
  so there's nothing that could sit around and expire. Flagged to the
  user as informational rather than assumed; no change made since a
  "coin credit wallet" that accumulates unspent would be a different
  design and wasn't confirmed as wanted.
- **Mbps settings**: confirmed a real gap. Bandwidth was 100% hardcoded
  in the `.rsc` files (`hs-default/gaming/high`, `pppoe-default/gaming/
  high` rate-limits) with zero admin-facing control for coin slot,
  voucher, or PPPoE.

User picked (via AskUserQuestion): **live-push via API** (not just a
documentation/manual-edit approach) and **shared per tier** (one
Download/Upload Mbps pair per tier, applied to both the hotspot and
pppoe profile of that tier, rather than separate fields per access
method) — this matches the existing tier-parity already present in the
`.rsc` files (hotspot and pppoe tiers have always used matching speeds).

**Implemented and approved.** Changes: `config.h` (`TIER1-3_DOWN/UP_MBPS`
defaults, mirroring the `.rsc` values); `admin_api.h` (`tier1-3Down/
UpMbps` settings fields, load/save, `rateLimitStringFor(tier)` — formats
RouterOS's `"rx/tx"` rate-limit syntax); `mikrotik_api.h` (new
`setHotspotProfileRateLimit()`/`setPppProfileRateLimit()` public
methods + a private `setRateLimitByName()` helper using `/ip/hotspot/
user/profile/set` and `/ppp/profile/set`); `gui_handler.h`
(`handleAdminGetSettings`/`handleAdminSaveSettings` extended; on save,
loops all 3 tiers pushing the new rate-limit to both the `hs-*` and
`pppoe-*` profile of that tier, returns `mikrotikPushed: true/false` so
the GUI can tell the admin whether it actually reached the router).
GUI: `admin.html` gained a "Bandwidth (Mbps)" Settings section (3 rows,
reusing the `.tier-price-row` responsive class); `script.js`'s
`saveSettings()`/`loadSettingsIntoForm()` extended, and the save
confirmation now reflects `mikrotikPushed` ("pushed live to MikroTik"
vs. "saved locally, could not reach MikroTik — try again once it's back
online"). `tools/mock_server.py` updated to match (always reports
`mikrotikPushed: true`, since the mock has no real-router-unreachable
state to simulate).

**Verified**: firmware compiled successfully (391,071 bytes flash /
34,776 bytes RAM, `.bin` 395,216 bytes — only the pre-existing SPIFFS-
deprecation and snprintf-truncation warnings, no errors). Tested
end-to-end in a real browser against the updated `tools/mock_server.py`:
Settings tab loads the correct default Mbps values (10/5, 30/10, 50/20,
matching the `.rsc` defaults), a changed value saves and persists
server-side, and the save confirmation correctly reports
`mikrotikPushed: true`. Re-verified mobile layout at 375px — the new
rows reuse the same `.tier-price-row` class from the earlier mobile fix
and stack correctly with no overflow. One real bug was caught during
this verification: the new fields were added to `saveSettings()` but
initially missed in `loadSettingsIntoForm()`, so the Settings tab
loaded with them blank — caught by checking actual input `.value`s in
the browser rather than trusting the code read-through, fixed before
reporting done.
**Not live-tested** — no real MikroTik router available in this
session to confirm the actual `/ip/hotspot/user/profile/set` and
`/ppp/profile/set` API calls land correctly; verified by code review
against the documented RouterOS API syntax and the existing patterns
already proven elsewhere in `mikrotik_api.h` (e.g. `removeByName`'s
find-id-then-act pattern).

## 2026-09-04 — Bug sweep ("ayusin lahat ng bugs")
User asked to fix all bugs, broadly. Went beyond the day's own
Bandwidth-settings diff and re-read the full login/session/pppoe path
end to end. Found and fixed two severe, previously-undetected bugs
(full detail in [09-changelog.md](09-changelog.md)):
1. **Hotspot logins never actually released the client from MikroTik's
   walled garden** — `/api/login` only registered the account via the
   RouterOS API; nothing ever submitted the browsing client's own
   credentials to MikroTik's real `/login` handler, which is what
   RouterOS requires to authenticate *that specific client's* session.
   This means every hotspot voucher/coin/subscriber login would have
   shown "Connected" on our status page while the customer stayed
   walled off with no real internet — arguably the most important bug
   in the whole project. Fixed by having `script.js` submit a real
   form POST to `/login` (with `dst` pointing back at `status.html`)
   right after `/api/login` succeeds, and again on Resume (since Pause
   fully deprovisions and kicks the walled-garden session).
2. **Subscriber accounts got the wrong MikroTik password** —
   `PPPoEManager::provision()` always used the session code as both
   username and password, correct for vouchers but silently wrong for
   subscribers (whose whole design point is a real, distinct password).
   Fixed by threading the actual verified password through `provision()`
   from both `handleLogin()` and (via a new optional field) `handleResume()`.

Also fixed 3 smaller issues surfaced while re-reviewing the same day's
Bandwidth (Mbps) settings work: a too-tight JSON response buffer that
risked silently dropping the `mikrotikPushed` field; `rateLimitStringFor()`
rounding fractional Mbps to whole numbers before pushing to MikroTik;
and an inconsistent `_mikrotikReachable` pre-check before the Mbps push
that could report a false negative off a flag that's only refreshed
every 30s.

**Verified**: firmware compiled clean (391,287 bytes flash / 34,760
bytes RAM). `tools/mock_server.py` gained a `/login` route simulating
MikroTik's redirect-on-success shape so the fix stays testable without
real hardware. Tested in a real browser: voucher hotspot login and
subscriber login (`room5`/`tenant123`) both now correctly go through
the `/login` redirect before landing on `status.html`; Pause→Resume
re-authenticates and returns to `status.html` correctly for both voucher
and subscriber sessions.
**Not live-tested** — no real MikroTik router available in this session
to confirm actual RouterOS `/login` behavior (CHAP handling, exact
redirect semantics) matches the mock's simplified simulation; the fix
follows RouterOS's documented `username`/`password`/`dst` POST
convention and the existing `.rsc` configs already permit plain PAP
(`login-by=http-chap,http-pap`).

## 2026-09-04 — Post-audit fixes (dynamic labels, brute-force lockout, regression test)
Re-audited the whole codebase per the user's request, then implemented
everything actionable without real MikroTik/NodeMCU hardware, in
priority order:

1. **Dynamic tier dropdown labels.** The audit re-confirmed the
   staleness gap flagged in the earlier session-work summary: 3
   dropdowns across `admin.html` (Vouchers, Subscriptions, Coin Slot
   Pricing) still showed hardcoded text even though tier price/time/
   data/Mbps are all Settings-editable now. Fixed with a new
   `GET /api/admin/tier-info` endpoint, open to both admin roles
   (unlike the full `/api/admin/settings`, super-only) since staff
   needs accurate pricing too when generating vouchers.
2. **Login brute-force protection.** Neither `/api/login` nor admin
   auth had any rate limiting - a real, exploitable gap. Added a small
   shared per-IP failure table (8 failures/minute → 429 lockout,
   self-clearing) covering both, careful not to count the normal
   "this code isn't a subscriber" fallthrough as a failure.
3. **`tools/regression_test.py`** - a 57-check automated contract test
   against `mock_server.py`'s full `/api/*` surface. Its first draft
   (running the mock server in-process via a threaded
   `ThreadingHTTPServer`) deadlocked on `/api/resume` specifically -
   isolated by testing the same endpoint against a real standalone
   `mock_server.py` process via curl (worked fine there), which
   pointed at the in-process GIL/threading setup as the actual bug.
   Fixed by launching `mock_server.py` as a real subprocess instead.

**Deliberately not attempted**: QR code generation for "Show QR"
(status.html still shows the code as text). A correct QR encoder
(Reed-Solomon error correction, mask evaluation, exact module
placement) is genuinely hard to get right from memory without real
verification tooling, and a subtly-broken one that *looks* fine but
doesn't scan is worse than the current honest fallback - consistent
with this project's existing stance on the QR *scanner*
(`BarcodeDetector`, chosen over a hand-rolled decoder for the same
reason). Left as a flagged gap rather than shipped half-verified.

**Verified**: firmware compiled clean (392,207 bytes flash / 35,000
bytes RAM). Brute-force lockout tested directly via curl (8 failures →
400, 9th → 429, confirmed it also blocks an otherwise-valid code and
blocks admin auth from the same IP - the table is shared). Tier-label
sync verified in a real browser (changed Tier 1 via the Settings API,
confirmed all 3 dropdowns updated; confirmed a staff account can read
`/api/admin/tier-info` but still gets 403 on full Settings). The new
regression suite passes all 57 checks.
**Not live-tested** — no real MikroTik/NodeMCU hardware available in
this session.

## 2026-09-04 — Salted password hashing
User asked to keep working through the "can do without hardware" list.
Picked up the MD5-hashing item flagged during the earlier audit:
`AdminAccount`/`Subscriber` passwords were stored as plain unsalted
`MD5(password)`, vulnerable to precomputed rainbow-table attacks
against a stolen `admins.json`/`subscribers.json`.

Added a random per-account `salt` field to both structs, generated via
`RANDOM_REG32` (the ESP8266's hardware RNG register — chosen over
Arduino's seeded `random()`, which is predictable from boot time) at
account-creation time; the stored hash becomes `MD5(salt + password)`.
Designed for **zero-migration backward compatibility**: an empty salt
(`""`, what every account created before this change already has)
makes `MD5("" + password)` mathematically identical to the old
`MD5(password)` — so nothing needs re-hashing, no forced password
reset, and the default `admin`/`ADMIN_PASSWORD_HASH` account seeded on
first boot keeps working exactly as before. Only new accounts (added
via the Admins/Subscriptions tabs after upgrading) get a real salt.

Explicitly scoped to salting only, not switching away from MD5
entirely (to a proper password KDF like bcrypt/argon2) - that would be
a much bigger change (different library, likely too slow/heavy for an
ESP8266 to compute per-login) and wasn't part of what was asked.

**Verified**: firmware compiled clean (392,719 bytes flash / 35,016
bytes RAM). Backward-compatibility math double-checked in Python
(`MD5("" + "admin")` reproduces the exact `ADMIN_PASSWORD_HASH`
constant; a random salt produces a different hash for the same
password, confirming the salting actually takes effect). The full
57-check regression suite was re-run afterward to confirm nothing else
in the admin/subscriber auth paths regressed.
**Not live-tested** — no real ESP8266 hardware in this session to
confirm `RANDOM_REG32` behaves as documented on actual silicon (it's a
standard, widely-used pattern, but this specific firmware build has
only run under PlatformIO's compiler, never on a real chip).

## 2026-09-04 — Critical fix: cross-origin GUI ↔ NodeMCU communication
User flagged an observation: voucher generation looked like it was
being saved to MikroTik, not NodeMCU. That was the visible symptom of
a real, severe bug — likely the most important single fix in this
project's history, and one that had been present (and completely
undetected) since the very first working build.

**Root cause**: the GUI is served by the MikroTik router; the JSON API
it calls is implemented by the NodeMCU. Two different devices, two
different IPs. Every `/api/*` call in `script.js` used a plain
relative path, which resolves against the *page's own origin* — the
MikroTik router, in production. RouterOS's hotspot webserver has no
`/api/*` routes, so every single dynamic feature (login, status,
pause/resume/extend, the entire admin dashboard including voucher
generation) would never actually reach the NodeMCU at all in a real
deployment. This had been invisible through every phase of this
project because `tools/mock_server.py` serves both the static GUI and
the API from one combined origin, so relative paths happened to work
there — the mock's convenience accidentally hid the single biggest bug
in the whole system.

**Fix**: `script.js` gained `apiBase()`, prefixing every `/api/*` call
with the NodeMCU's IP explicitly (`NODEMCU_IP` — must be kept in sync
with `firmware/config.h`'s constant of the same name) except when
already served directly from the NodeMCU or `mock_server.py`. Since
this makes real requests genuinely cross-origin, `gui_handler.h` added
CORS support (allow-all origin — safe here, no cookie/session auth to
steal cross-site; `onNotFound()` answers preflight `OPTIONS`
requests). `mock_server.py` mirrors both, so the fix is testable
without hardware.

**Verified**: firmware compiled clean (393,019 bytes flash / 35,128
bytes RAM). Ran two `mock_server.py` instances on different local
ports — genuinely different browser origins, not a same-origin
simulation — and confirmed in a real browser: a cross-origin GET
succeeds, and a cross-origin GET carrying `X-Admin-*` headers (which
triggers a real CORS preflight) also succeeds, end to end. Separately
confirmed `apiBase()`'s decision table is correct for every real
hostname (NodeMCU's own IP, dev/testing hosts → relative; MikroTik's
`hotspot-address`/`dns-name` → absolute NodeMCU URL). Full regression
suite re-run clean.
**Not live-tested** — no real MikroTik/NodeMCU hardware in this
session; this is now, by a wide margin, the single highest-priority
thing to confirm the moment real hardware is available, since without
it nothing in the GUI would have worked at all.

## 2026-09-04 — Original-JuanFi audit + First-boot Setup Wizard
User asked for a thorough re-check against the real original JuanFi
(github.com/ivanalayan15/JuanFi) — the explicit goal being that this
project should be a strict superset of the original's real features,
not something that silently dropped pieces of it. Fetched the actual
repo (file tree, README, on-login MikroTik script) rather than trusting
the existing `docs/00-github-comparison.md`, which turned out to have
at least one flatly wrong claim ("Telegram API preserved" — never
actually implemented here). Full corrected feature-by-feature table now
lives in that doc.

**Biggest real gap found**: the original ships a first-boot setup
wizard (NodeMCU broadcasts its own WiFi AP, serves a config form) so an
installer never has to open Arduino IDE/PlatformIO or edit `config.h`
per deployment. This project required exactly that for every WiFi/
MikroTik-credential change — a genuine practical weakness for anyone
selling/installing multiple units. User picked this as the first thing
to build, via full architectural brainstorming (classified
architectural, not bounded, since it's a genuinely new subsystem: an
AP+captive-portal mode the firmware runs INSTEAD of normal operation).

**Design decisions** (each confirmed with the user before building):
1. Enter Setup Mode automatically on a fresh device (no saved config) -
   not "always available at boot," to avoid interrupting normal startup.
2. Reconfigure later via the onboard FLASH button (GPIO0, held during
   boot) only - deliberately no software-only reset button in the
   Admin dashboard, since that would be unreachable in exactly the
   failure mode (bad saved network config) where you'd need it.
3. Given the wizard makes network config genuinely dynamic now, use
   **mDNS** (`juanfi-nodemcu.local`) instead of a hardcoded IP for
   `script.js`'s `apiBase()` to find the NodeMCU - a hardcoded IP would
   only work if paired with a manually-configured static DHCP lease,
   the same two-places-must-match fragility that caused the
   cross-origin bug fixed earlier the same day.
4. Wizard collects WiFi SSID/password, MikroTik host/API user/password,
   AND an initial admin dashboard password (replacing the shipped
   "admin"/"admin" default with something the installer actually
   chooses) - not just network fields.

**Implemented**: `firmware/network_config.h` (new, `/network.json`
persistence, zero-migration - a device with no saved config falls back
to the existing `config.h` constants unchanged); `firmware/setup_mode.h`
(new, `SetupModeManager` - AP mode + `DNSServer` captive-portal
redirect + one embedded HTML form, using only libraries already bundled
with the `esp8266` Arduino core); `MikrotikAPI` gained runtime
`host`/`apiUser`/`apiPass` fields (default to the `config.h` constants);
`AdminAPI::begin()`/`loadAdminAccounts()` gained an optional
`initialAdminPassword` parameter, seeding the first super admin with
it (salted, reusing the 2026-09-04 salted-password work) when present;
`nodemcu_firmware.ino` branches into Setup Mode at the top of `setup()`
before anything else runs, and `MDNS.begin(NODEMCU_MDNS_NAME)` after a
successful WiFi connect in normal mode; `script.js`'s `apiBase()`
switched from a hardcoded `NODEMCU_IP` to `NODEMCU_HOST =
'juanfi-nodemcu.local'`.

Also corrected `docs/00-github-comparison.md` with an honest
feature-by-feature table and flagged the rest of what the audit found
for future prioritization (not built yet, not assumed): Telegram
notifications, a physical "Night Light" relay (distinct from this
project's Night Promo pricing discount, despite the similar name),
remote restart via HTTP, real QR code generation (the original vendors
`qrcode.min.js`, a known-good library - worth adopting now that a
vetted option is identified, rather than the hand-rolled encoder
declined earlier this session), LAN/Ethernet + ESP32 hardware options,
and multi-vendo (multiple coin-slot units per router) support.

**Verified**: firmware compiled clean (423,991 bytes flash / 36,236
bytes RAM). Full 57-check regression suite re-run clean (Setup Mode
itself has no HTTP API surface to mock - pure boot-time AP behavior -
so the suite instead confirms the existing `/api/*` contract didn't
regress from the `MikrotikAPI`/`AdminAPI` signature changes).
**Not live-tested** — no real NodeMCU hardware in this session to
confirm AP-mode captive-portal detection (does it actually trigger the
"Sign in to network" prompt on real phones/laptops?) or mDNS resolution
against real client devices. This is now, alongside the walled-garden
and cross-origin fixes from earlier the same day, one of the
highest-priority things to confirm the moment real hardware is
available.

## 2026-09 — Full GUI visual redesign ("Coin Meter" theme)
User confirmed they wanted a full visual redesign, not incremental
polish on the old teal glass-morphism theme — informed by the original
JuanFi's own template versions as design inspiration, not a from-scratch
guess. Classified architectural (a full visual overhaul across all 3
GUI pages) and brainstormed via the `frontend-design` skill before
touching code.

**Research**: fetched the original's actual latest template CSS
(`mikrotik-template/4.3/assets/css/JuanFi.css`) and login page rather
than guessing at its look. Found a consistent "coin-op utility meter"
vocabulary across every version: dark blue-gray palette, thick 2px
borders, a signature 4-color rainbow gradient divider bar, blinking
red/green connection-status text, and big bold numbers with small gray
labels underneath (coin total, time remaining) — a utilitarian,
high-legibility "meter readout" aesthetic, not a sleek fintech-app
look. This became the grounding subject matter for the redesign,
instead of defaulting to a generic "modern dark SaaS dashboard."

**Design decisions**:
- New palette: brass/amber (`--accent`, replacing the old generic
  AI-dark-teal `#00d4aa`) as the rebrandable primary color, plus FIXED
  (non-rebrandable) `--peso` (green, success/money) and `--alert` (red,
  danger/expired) semantic colors — a shop's brand color shouldn't
  change whether "online" reads as good or bad.
- System fonts only, no Google Fonts/CDN — a hard technical
  requirement, not a style choice: login.html/status.html are shown to
  a customer who is still walled off from the internet at that point,
  so an external font/CDN request would just hang or fail. Matches the
  original's own fully-self-contained approach.
- Monospace/tabular-nums treatment for all "meter readout" numbers
  (time remaining, data remaining, voucher codes, peso amounts, admin
  stat cards) — a deliberate, subject-grounded choice (a coin meter
  reads like a utility meter or calculator display), not just "pick a
  font and move on."
- Signature element: the voucher code input styled as a literal coin
  slot (dark inset pill, monospace, letter-spaced) — the one place this
  redesign spends its boldness, per the skill's restraint principle;
  everything else stays quiet around it. A refined 2-stop brass→peso
  gradient bar across the top of every card is a quieter nod to the
  original's 4-color rainbow divider, without directly copying it.
- Structural HTML/class names were NOT changed — this is a CSS + a
  couple of small script.js color-reference redesign, not a markup
  rewrite, so no `script.js` DOM logic needed touching beyond fixing
  two stray hardcoded hex colors that should have referenced the new
  semantic variables (the "online" status indicator dot was reading
  `var(--accent)`/a hardcoded `#e17055` instead of the new `--peso`/
  `--alert`).

**Verified**: real browser screenshots across login.html, status.html
(including the QR modal), and admin.html (Overview, Vouchers tabs),
at both desktop and mobile (375px) widths — all read cleanly, the
"meter readout" tabular numbers work as intended, and the existing
mobile-responsiveness fixes (tabs chevron, tier-price-row stacking)
still hold. Also caught and fixed a real default-brand-color mismatch
during testing: `AdminAPI::brandColor`, `mock_server.py`'s default
settings, and the admin.html color picker's default were all still the
OLD teal (`#00d4aa`) — since `applyBranding()` unconditionally
overrides the CSS default with whatever `/api/branding` returns, the
new palette was invisible until these were updated to the new brass
default too. Firmware recompiled clean (532,759 bytes flash / 37,052
bytes RAM — see the Telegram entry above for why this crossed the old
500KB budget). Full regression suite re-run clean.
**Not live-tested** — no real device/browser diversity tested beyond
this session's Browser pane (Chromium-based); worth a look on Safari/
iOS given phones are the primary customer device.

## 2026-09-08 — Real RouterOS API testing rig (CHR + QEMU) + a genuine `run()` bug found and fixed
User asked whether an on-device emulator could help close the biggest
standing verification gap: `firmware/mikrotik_api.h` (the NodeMCU's
RouterOS binary API client, port 8728) had **never once run against a
real RouterOS instance** — `tools/mock_server.py` only mocks the
NodeMCU's own JSON HTTP contract to the browser GUI, not the RouterOS
API wire protocol at all. Every claim about `kickActiveHotspotUser()`
and the random-MAC fix being "correct" was reasoning from the API
docs, never verified execution.

No local device emulator existed (no VirtualBox, Hyper-V disabled,
no Android SDK). Built a real one instead, with no admin/root access
anywhere in the chain: downloaded MikroTik CHR (a real RouterOS build
for VMs, free for testing) and ran it under QEMU inside the existing
WSL2 Ubuntu install. `sudo` was unavailable non-interactively (no way
to relay a live password prompt through this session), so every
package QEMU needed — `qemu-system-x86`, `qemu-utils`, `seabios` (the
PC BIOS blob QEMU wouldn't boot without), and roughly a dozen
transitive shared libraries chased down one `ldd` pass at a time — was
pulled with `apt-get download` (root not required) and extracted by
hand with `dpkg-deb -x` into a user-owned prefix. Runs under pure
software emulation (TCG, no KVM — the WSL user isn't in the `kvm`
group and fixing that needs root); CHR boots in about 2 minutes this
way, fine for protocol testing. RouterOS's console does VT100
cursor-position detection (`ESC[6n`) before showing the login prompt,
so a small persistent Python bridge (`serial_bridge.py`) answers that
over a Unix-socket + FIFO pair, keeping one long-lived connection so
the guest's getty never sees a spurious disconnect mid-login.

Wrote `ros_api_test.py`: a Python client implementing the exact same
wire format as `mikrotik_api.h` (length-prefixed words, `=key=value`
attributes, `!re`/`!done`/`!trap` sentences) and driving the firmware's
actual command sequences — login, `addHotspotUser`/`removeHotspotUser`,
`kickActiveHotspotUser`, `addPppSecret`/`removePppSecret`,
`getActiveTraffic` — against the live CHR instance.

**Found a real bug this way**: `MikrotikAPI::run()` returned
immediately on a RouterOS `!trap` reply, without reading the `!done`
sentence RouterOS *always* sends right after a `!trap` — confirmed on
the wire against real RouterOS 7.16.2 (not assumed from docs). That
leftover `!done` stays unread in the TCP buffer, so the next API
call's first `readReply()` consumes it instead of its own command's
real reply — silently shifting every subsequent reply on that
connection by one sentence, for the life of the connection (`_client`
is a long-lived member reused across calls via `begin()`). Reproduced
directly: after one deliberately-failing command (a bad profile
name), `removeHotspotUser()` reported success while the hotspot user
stayed in the router's table, and a later `/ppp/secret/print` query
returned a `/ppp/profile/print` result instead. Any real RouterOS-side
rejection in production — a typo'd profile, a stale `.id`, literally
any `!trap` — would have silently corrupted every command after it
until the device's next reconnect. `mock_server.py` structurally could
never have caught this.

**Fix**: `run()` now keeps looping through a `!trap` (remembering the
command failed) instead of returning immediately, and only returns
once the guaranteed trailing `!done` actually arrives, so the
connection never desyncs. See [mikrotik_api.h](../firmware/mikrotik_api.h).

**Verified**: `ros_api_test.py` re-run against the same live CHR VM
post-fix — 19/19 checks pass, including the deliberate-trap case no
longer desyncing anything that follows it. Firmware recompiled clean
(532,775 bytes flash / 37,052 bytes RAM — 16 bytes over the pre-fix
build). Full 60-check `regression_test.py` suite still clean.
**Not live-tested** on physical hardware — this closes the "is the
RouterOS API protocol usage actually correct" gap specifically; the
"real ESP8266 + real phone + real MikroTik box" gap (item 4 below)
still needs physical hardware, though the API calls it depends on are
now proven correct against real RouterOS rather than just reasoned
about. The CHR VM and test scripts are scratch/dev artifacts (not part
of the shipped project) and were left running in WSL2 for any
follow-up testing.

## 2026-09-22 — Rebrand to ZxheiFi, logo re-theme, ZxheiFi Setup Companion app
User asked to rename the project to their own brand "ZxheiFi" (own
logo, own color theme) and to build two deployment tools - a firmware
flasher and a checkbox-driven MikroTik configurator. See
[09-changelog.md](09-changelog.md)'s matching entry for full detail;
summary here:

- **Real hardware arrived**: NodeMCU on COM5. First-ever real flash +
  boot + Setup-Mode-AP-broadcasting confirmation (via live WiFi scan),
  both before and after the rebrand.
- **Re-themed** `mikrotik/gui/style.css` from real logo pixel colors
  (Pillow-sampled, not guessed); processed the logo's white background
  to transparent via border-flood-fill so its own white wordmark text
  survived; found/fixed 4 buttons/gradients still hardcoded to the old
  brass theme instead of using the `--accent`/`--panel` variables.
- **Rebrand swept** ~40 files (SSIDs, mDNS hostname, API username, docs,
  README) - historical changelog/roadmap entries and the real
  upstream-comparison doc deliberately left untouched.
- **Built `desktop-app/`**: Flash Firmware tab (esptool + serial
  monitor) and Configure MikroTik tab (checklist-driven RouterOS API
  execution, reusing the validated client from the entry above).
  Dry-run testing against the CHR rig found **4 more real RouterOS
  parameter bugs** never caught before (same root cause as the `run()`
  bug: nothing had run these exact commands against real RouterOS
  either) - `cookie-max-age`→`http-cookie-lifetime`, a nonexistent
  `use-vj-compression` removed, `fq_codel`→`fq-codel` (hyphen, not
  underscore - kind value and all sub-params), and
  `idle-timeout`/`keepalive-timeout` moved from the hotspot profile to
  the hotspot server command. Fixed in both `mikrotik_commands.py` and
  the two `.rsc` files so they can't drift apart.
- **Verified**: firmware recompiled clean, 60-check regression suite
  clean, reflashed real hardware **using the new flasher tool itself**
  (confirmed new boot banner + AP name on real hardware), 43/43
  interface-valid steps in a dry-run against a freshly factory-reset
  CHR router (remaining 9 failures are this single-NIC/no-wireless test
  VM's own limitation, not command defects).
- **Not yet done**: packaging `desktop-app/` as a distributable `.exe`;
  the physical hAP lite's real WiFi/PoE behavior still needs the user's
  own hardware on LAN + Winbox.

## Remaining
- **Phase E** — remote/cloud management + e-load integration (needs an
  architecture decision first: cloud relay vs. port-forward vs.
  MikroTik Cloud; original JuanFi's own reference architecture —
  `juanfiapi.projectdorsu.com` + the "JuanFi Manager" Android app,
  found during the 2026-09-04 audit — is worth studying when this gets
  picked up)
- Multi-LAN/multi-zone portals (flagged under Network Control, not yet
  scoped into a phase — surface if it's actually wanted)
- Possible follow-up: a coin-credit "wallet" that accumulates unspent
  credit (with its own validity) instead of instant-spending on every
  insertion — flagged during the 2026-09-04 audit but not confirmed as
  wanted; only scope this if the user actually asks for it.
- Switching admin/subscriber password storage from salted-MD5 to a
  proper password KDF (bcrypt/argon2/scrypt) — salting (done
  2026-09-04) addresses rainbow tables but MD5 itself is still a fast
  hash; only worth the added library/compute cost if actually needed.
- **"Night Light" physical relay control** — distinct from this
  project's Night Promo pricing discount despite the similar name;
  original's is a literal light-bulb toggle via relay
  (`/admin/api/toggerNightLight`). `PIN_RELAY` exists in `config.h` but
  nothing toggles it yet.
- **Remote restart via HTTP** — original has `/admin/api/restartSystem`
  with an `X-TOKEN` header; not implemented here.
- **Admin-configurable coin anti-abuse thresholds** — `MIN_PULSE_
  INTERVAL_US`/`MAX_PLAUSIBLE_PULSES` are compile-time constants;
  original's "coinslot abuse system config" is admin-editable. Small,
  low-risk win if picked up.
- LAN-based (Ethernet/W5500) and ESP32 hardware options — original
  supports both; this project is ESP8266-WiFi-only. Real gap, but a
  significant one to scope (new hardware target).
- Multi-vendo (multiple coin-slot units reporting to one router) —
  this project assumes a single NodeMCU per deployment.
- LCD on-device voucher generation — hardware-dependent (needs an LCD
  screen wired in), not scoped.

### Highest priority once real hardware is available
None of these has ever run against real MikroTik/NodeMCU hardware —
all are foundational to the whole system actually working, so all need
confirming before any real deployment, in this order:
1. **The cross-origin `apiBase()`/CORS fix (2026-09-04)** — without
   this, the GUI can't reach the NodeMCU's API *at all* in production.
2. **The First-boot Setup Wizard + mDNS (2026-09-04, above)** — confirm
   the captive-portal AP actually triggers a "Sign in to network"
   prompt on real phones/laptops, and that `zxheifi-nodemcu.local`
   actually resolves from a browser on the hotspot network.
3. **The walled-garden login fix (2026-09-04 bug sweep entry, earlier
   in this log)** — confirms RouterOS's real `/login` handler accepts
   the POST shape being sent (CHAP challenge behavior, `dst` redirect
   semantics) and actually releases the walled client.
4. **The random-MAC reconnect fix (2026-09-04, below)** — confirms
   `shared-users=1` actually blocks a second MAC without
   `kickActiveHotspotUser()`, and that the fix actually resolves it.
   The underlying RouterOS API calls themselves (`kickActiveHotspotUser`'s
   query+remove, `addHotspotUser`, etc.) are now verified correct
   against real RouterOS via the CHR/QEMU rig (2026-09-08, above) — what
   still needs real hardware is the `shared-users=1` blocking behavior
   itself with an actual second MAC on real WiFi, which CHR can't
   exercise (it has no wireless hardware to emulate).

## 2026-09-04 — Category C follow-through: random MAC, QR generation, Telegram
User asked to work through the remaining well-scoped items from the
original-JuanFi audit (Category C: clear scope, no blocking decision
needed) before starting the GUI redesign, with proper planning/review
first. Tackled in priority order:

1. **Random MAC reconnect fix.** Investigated first, per the user's
   request, rather than assumed - confirmed this project genuinely was
   affected: `redeemVoucher()`'s single-use check would reject a
   customer's own already-paid voucher code if their phone randomized
   its MAC mid-session (common on modern Android/iOS) and re-triggered
   the hotspot walled garden under a new MAC, AND separately,
   `MikrotikAPI::addHotspotUser()` never cleaned up the stale
   `/ip/hotspot/active` entry from the old MAC, which would block the
   new MAC's login under the hotspot profile's `shared-users=1` even if
   the voucher check were bypassed. Fixed both: a new
   `kickActiveHotspotUser()` (deliberately NOT folded into
   `addHotspotUser()` itself, since Extend also uses that path and
   kicking a still-connected client mid-extend would be a regression),
   called explicitly from `handleLogin()`'s login paths only; and a new
   reconnect branch in `handleLogin()` that recognizes an
   already-used-but-still-ACTIVE voucher code and resumes it with its
   current remaining budget instead of rejecting it.
2. **Real QR code generation.** Fetched the original JuanFi's own
   `qrcode.min.js` directly from its repo (davidshimjs/qrcodejs, MIT)
   rather than hand-rolling one - the risk calculus that led to
   declining this earlier (an unverifiable hand-rolled encoder) doesn't
   apply to a known, community-vetted library. Scanned the fetched file
   for anything suspicious (no `eval`, network calls, or cookie access)
   before vendoring. Verified with genuine round-trip decoding: a real
   QR generated in a live browser, decoded back with an *independent*
   decoder (jsQR, used only for this verification, not vendored),
   confirmed to exactly match the original session code - the strongest
   verification available without a physical scanner.
3. **Telegram notifications.** Real gap from the audit, closed. This
   project's architecture doesn't send from RouterOS scripts like the
   original does, so `firmware/telegram.h` implements a NodeMCU-side
   HTTPS client to the Bot API instead, with messages queued and sent
   one at a time from the main loop (never inline in a request handler,
   since the TLS handshake can take 1-3+ seconds) - fires on genuine new
   sales (voucher/subscriber/coin), not reconnects. Admin-editable via
   Settings, unlike the original's config-file-only approach.

Also corrected `docs/00-github-comparison.md`'s status table to reflect
all three as ✅ now.

**Verified**: firmware compiled clean after each of the three changes
(random MAC + QR generation confirmed via the regression suite, now 60
checks; Telegram's build is the largest single flash-size addition
this session - 532,759 bytes / 37,052 bytes RAM, ~100KB more than
before, since `ESP8266HTTPClient`+`WiFiClientSecureBearSSL` pull in the
full BearSSL/TLS stack - enough to push past the project's old 500KB
round-number budget, which was accordingly raised to 900KB to reflect
the actual ~1,044,464-byte partition ceiling; still only ~51% used).
`tools/mock_server.py` and `tools/regression_test.py` updated with a
matching reconnect check.
**Not live-tested**: the random-MAC fix needs a real MikroTik router to
confirm `shared-users=1` actually blocks a second MAC (and that the fix
resolves it); QR generation was verified in software (round-trip
decode) but not against a physical phone camera; Telegram was not
tested against a real bot token/chat ID.
