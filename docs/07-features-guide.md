# 🧩 Features Guide

How each "Enhanced" feature from
[00-github-comparison.md](00-github-comparison.md) actually works,
and which file implements it.

## Cross-Origin GUI ↔ NodeMCU Communication
The GUI is served BY the MikroTik router (see
[06-gui-customize.md](06-gui-customize.md)); the JSON API it calls is
implemented by the NodeMCU — a **different device** (`hotspot-address=
10.0.0.1` for MikroTik per the `.rsc` configs; the NodeMCU is reached
by name, see below). A same-origin relative `fetch('/api/...')` from a
page served at `10.0.0.1` resolves against *that* origin — the
MikroTik router's own webserver, which has no such routes — never
reaching the NodeMCU at all. This was a real, previously undetected
bug affecting every single `/api/*` call in the entire project (login,
status, admin dashboard, voucher generation — all of it), masked
completely throughout earlier development because `tools/mock_server.py`
happens to serve both the static GUI and the API from the same origin,
so relative paths "just worked" there.

Fixed with `script.js`'s `apiBase()`: every `/api/*` call is prefixed
with the NodeMCU's absolute URL (`http://` + `NODEMCU_HOST`, currently
`nodemcu.zxheifi.lan` — a static DNS entry on the router rather than a
hardcoded IP; it replaced the mDNS name `zxheifi-nodemcu.local`, which
phone captive-portal browsers can't resolve. `NODEMCU_HOST` must match
`desktop-app/mikrotik_commands.py`'s `NODEMCU_HOSTNAME`) *unless* the page
is already being served directly from the NodeMCU or from
`mock_server.py` (hostname is the mDNS name, `localhost`, or
`127.0.0.1` — dev/testing cases where a relative path is correct and
avoids unnecessary cross-origin overhead). Because this makes every
real request genuinely cross-origin, `firmware/gui_handler.h` gained
CORS support: `setCorsHeaders()` (allow-all origin, since this is a
local captive-portal API with no cookie/session state to steal
cross-site — the real reason CORS is needed here is the two-device
split, not a security boundary being relaxed) on every response, plus
an `onNotFound()` handler answering the browser's CORS preflight
(`OPTIONS`) requests, since no route is registered for that method.
`tools/mock_server.py` mirrors both (CORS headers + `do_OPTIONS`) so
the fix is exercisable without real hardware — verified with two mock
server instances on different local ports acting as genuinely separate
origins, real cross-origin `fetch()` calls (including one sending the
`X-Admin-*` headers that trigger a real preflight) succeeding in an
actual browser.

## Random MAC Reconnect Handling
Modern phones frequently randomize their WiFi MAC address on
reconnect (a privacy feature), which re-triggers MikroTik's hotspot
walled garden under a **new** MAC even for a customer already mid-session
— the original JuanFi has an explicit fix for this
(`isRandomMacSyncFix` in its on-login script); this project's
architecture is different enough that the equivalent fix had to be
designed separately (found + fixed 2026-09-04). Two distinct problems,
both solved:

1. **MikroTik-side**: `addHotspotUser()` (used by both fresh login and
   Extend) never removes a stale `/ip/hotspot/active` entry — fine for
   Extend (the same MAC is still connected, kicking it would be a
   regression), but means a reconnect under a new MAC would be blocked
   by the hotspot profile's `shared-users=1` even after re-provisioning,
   since the old MAC's entry is still occupying that slot. Fixed with a
   new `MikrotikAPI::kickActiveHotspotUser()`, called explicitly from
   `handleLogin()`'s login paths (never from Extend/Resume) right
   before provisioning.
2. **NodeMCU-side**: a voucher is single-use, so resubmitting the SAME
   code (the only thing a customer has to re-authenticate with, since
   the MAC change happens transparently to them) would normally be
   rejected by `AdminAPI::redeemVoucher()` as `invalid_or_used_voucher`
   — indistinguishable, from the customer's perspective, from a genuine
   invalid code. Fixed in `handleLogin()`: before attempting
   `redeemVoucher()`, check whether the code already has an ACTIVE
   `SessionManager` entry (`isSubscriber == false`); if so, treat it as
   a **reconnect** and re-provision with that session's *current*
   remaining time/data (resuming it first if it was paused) instead of
   re-redeeming — logged as `voucher_reconnect`, distinct from
   `voucher_login`, in the Activity Log. Subscriber logins already
   handled this correctly without changes (`checkSubscriberLogin()`
   never marks anything "used"), so only the `kickActiveHotspotUser()`
   call was needed on that path.

**Verified**: firmware compiled clean (424,647 bytes flash / 36,252
bytes RAM). `tools/mock_server.py` and `tools/regression_test.py`
updated with a matching reconnect check (resubmitting an
already-used, still-active voucher code returns 200 with the same
session, not a rejection).
**Not live-tested** — no real MikroTik hardware in this session to
confirm `shared-users=1` actually blocks a second MAC without this fix,
or that `kickActiveHotspotUser()`'s RouterOS command sequence behaves
as expected against a real router.

## Real QR Code Generation (Show QR)
`status.html`'s "Show QR" used to display the session code as plain
text — a deliberate, honest fallback rather than shipping a hand-rolled
QR encoder that couldn't be verified. Fixed by vendoring
`mikrotik/gui/qrcode.min.js`, the exact same library
([davidshimjs/qrcodejs](https://github.com/davidshimjs/qrcodejs), MIT)
the original JuanFi project ships and uses in production — fetched
directly from `ivanalayan15/JuanFi`'s own repo and scanned for anything
suspicious (no `eval`, no network calls, no cookie access) before
vendoring, rather than trusting it blindly. `script.js`'s `showQR()`
now calls `new QRCode(qrCode, { text: sessionId, ... })`, encoding the
plain session code as text — matching exactly what `login.html`'s
`scanQR()` (`BarcodeDetector`) already expects to read back, so no
change was needed on the scanning side.

**Verified with round-trip decoding**, the strongest check possible
without a physical scanner: generated a real QR in a live browser,
then decoded the resulting `<canvas>` pixel data with an *independent*
decoder (jsQR — used only for this one-time verification, not
vendored into the project) and confirmed the decoded text exactly
matched the original session code. This is a materially stronger
verification than the walled-garden/CORS fixes could get (those need
real MikroTik hardware); QR encode→decode correctness is fully
verifiable in software alone.
**Not live-tested with a physical phone camera** — software round-trip
verified, but an actual phone's camera/QR scanner app was not tested
against the printed/displayed result.

## Telegram Notifications
Real gap closed from the 2026-09-04 original-JuanFi audit —
`TELEGRAM_BOT_TOKEN`/`TELEGRAM_CHAT_ID` existed in `config.h` as
placeholders since Phase 0, but nothing ever sent anything (a previous
version of `docs/00-github-comparison.md` incorrectly claimed this was
"preserved"). The original sends **from the router** via a RouterOS
on-login script (`/tool fetch` straight to the Bot API); this project
keeps everything NodeMCU-side, so `firmware/telegram.h`'s
`TelegramNotifier` is a NodeMCU HTTPS client instead
(`ESP8266HTTPClient` + `WiFiClientSecureBearSSL`, `setInsecure()` — no
certificate pinning, a standard tradeoff for ESP8266 projects given the
RAM/complexity cost of a real cert store on this platform).

Messages are **queued, never sent inline** in a request handler — the
TLS handshake to `api.telegram.org` can take 1-3+ seconds, which would
otherwise delay the customer's own login response. `queueMessage()`
pushes onto a small fixed-size ring buffer (`TELEGRAM_QUEUE_SIZE`,
default 8 — oldest dropped if full, matching this project's existing
RAM-bounded-fixed-array style, e.g. the login-attempt table); the main
`loop()` calls `TelegramNotifier::loop()`, which sends at most one
queued message per iteration. A message that can't be sent (WiFi down,
Telegram unreachable) is simply dropped rather than retried — a missed
sales notification isn't worth risking an unbounded backlog on an
80KB-RAM device.

Fires on genuinely new sales only — voucher redemption, subscriber
login, and coin credit — **not** on a reconnect (see Random MAC
Reconnect Handling above), since that isn't a new sale. Admin-editable
via Settings → Telegram Notifications (enable toggle, bot token, chat
ID) rather than `config.h`-only, so a token/chat ID change doesn't need
a reflash; `GUIHandler` keeps a live copy of these three fields synced
from `AdminAPI` on every Settings save.

**Verified**: firmware compiled clean at 532,759 bytes flash / 37,052
bytes RAM — the `ESP8266HTTPClient`/`WiFiClientSecureBearSSL` (BearSSL/
TLS) stack this pulls in is by far the largest single flash-size
addition this project has made in one change (~100KB), enough to push
the build past the old 500KB round-number budget - see config.h's
SIZE CONSTRAINTS section, raised to 900KB to reflect the *real*
ceiling (the "4M (3M SPIFFS)" board layout's ~1,044,464-byte app
partition), which this build still uses only ~51% of.
`tools/mock_server.py`'s settings dict mirrors the 3 new fields so the
Settings round-trip stays testable.
**Not live-tested** — no real Telegram bot token or NodeMCU hardware in
this session to confirm an actual message is delivered; the HTTPS
POST shape follows Telegram's documented Bot API (`sendMessage` with
`chat_id`/`text` as `application/x-www-form-urlencoded`).

## Dual Mode (Hotspot + PPPoE)
The router runs both a Hotspot server (`10.0.0.0/24`) and a PPPoE
server (`10.0.10.0/24`) at once — separate subnets, no conflict (see
either `.rsc` config, section "PPPOE SERVER"). The GUI's mode toggle
(`login.html`) just changes which form posts to `/api/login`; the
firmware (`firmware/pppoe.h`) decides whether to create a MikroTik
Hotspot user or a PPP secret based on the `mode` field.

For **hotspot mode specifically**, `/api/login` succeeding is only
half the story — it registers the account with MikroTik over the
RouterOS API (NodeMCU-to-router), but that alone doesn't release *this
browsing client* from the hotspot's walled garden. RouterOS only does
that once the client itself authenticates against the router's own
`/login` handler, so `script.js`'s `submitMikrotikHotspotLogin()`
submits a real form POST there right after `/api/login` succeeds
(`dst` points it back at `status.html` instead of RouterOS's default
post-login page). The same re-authentication happens again on
**Resume**, since Pause fully deprovisions the account and kicks the
active walled-garden session. PPPoE mode skips this entirely — that's
a separate dial-up connection made by the customer's own PPPoE client,
outside the browser.

## Time + Data Combo (and per-tier Time-only / Data-only mode)
Both budgets are tracked together per session
(`firmware/session.h`'s `SessionEntry`). Every 10 seconds
(`DATA_QUERY_INTERVAL` in `config.h`) the firmware polls MikroTik for
bytes used and deducts the delta from the data budget; every 1 second
it deducts from the time budget. Whichever hits zero first ends the
session (`SessionManager::isOver`).

Each Rate Profile (Settings → Rate Profiles, see below) sets its own
`dataMb` — `0` means unlimited data, only the clock counts down
(time-only); any positive value counts down both together (combo).
There's no separate data-only/unlimited-time mode - every profile
always has real minutes. "Unlimited" swaps in the `UNLIMITED_SECONDS`/
`UNLIMITED_BYTES` sentinels from `config.h`; the existing countdown
code just never runs out on that side, and `MikrotikAPI::addHotspotUser()`
omits that RouterOS limit parameter entirely rather than sending a huge
number. Bandwidth *speed* is unaffected either way — that's still
capped by the tier's MikroTik profile (`hs-default`/`hs-gaming`/`hs-high`).

## Pause / Resume (manual)
Different from Idle Auto-Pause below - this is the user clicking a
"Pause" button on `status.html` before stepping away. Unlike idle-pause,
it actually deprovisions the MikroTik user (`GUIHandler::handlePause`),
so paused time isn't free internet, and re-provisions on Resume with
whatever budget was left. A session left paused longer than
`MAX_PAUSE_MINUTES` (default 120) is dropped outright
(`SessionManager::isPausedTooLong`, checked in `queryActiveSessions()`)
so an abandoned session doesn't reserve a slot forever.

## Subscriptions
A recurring account (boarding house tenant, monthly customer) instead
of a single-use voucher — `firmware/admin_api.h`'s `Subscriber` struct,
managed from the admin dashboard's Subscriptions tab. Admin-managed
billing only (no online payment gateway): the admin adds a
username/password and an initial period, then clicks "+30d" to renew
as payments come in. While active, a subscriber gets unlimited time and
data (same `UNLIMITED_*` sentinels as Time-only/Data-only mode above) -
enforcement is purely the expiry date, checked once a minute
(`checkSubscriptions()` in the `.ino`) and cutting access off
immediately on lapse, no grace period. Login reuses the same
username/password fields already on `login.html` (previously unused by
vouchers); `GUIHandler::handleLogin()` checks the subscriber table
before falling back to the voucher pool.

## Idle Auto-Pause
While no meaningful traffic is flowing (less than
`IDLE_ACTIVITY_BYTES`, default 50KB, between polls), the session's time
budget stops counting down — `SessionManager::tickSeconds()` checks
`isIdle()` before deducting. Data budget is unaffected either way,
since idle traffic by definition isn't using data.

## Night Promo
`firmware/qos.h`'s `QoSManager::isNightPromoActive()` checks the wall
clock (synced via NTP in `setup()`) against `NIGHT_PROMO_START_HOUR`/
`END_HOUR` in `config.h`. When active and enabled (admin can toggle it
off in the dashboard), any tier-1/2/3 voucher redeemed during that
window gets its data allowance doubled
(`NIGHT_PROMO_DATA_MULT`) — see `handleLogin()` in
`firmware/gui_handler.h`. There's also a dedicated "night" voucher
product (`vouchers/generator.py --tier night`) for shops that prefer
selling a fixed-price night ticket instead of relying on the automatic
multiplier.

## Gaming QoS
Pure MikroTik-side (`mikrotik/gaming_qos_fq_codel.rsc`): common gaming
UDP ports (Xbox Live, PSN, Steam, Valorant, etc.) get packet-marked and
routed through a priority-1 fq_codel queue, ahead of general browsing
traffic. Tier 2/3 vouchers also get a MikroTik user profile
(`hs-gaming`/`pppoe-gaming`, `hs-high`/`pppoe-high`) with a higher
rate-limit ceiling — see `QoSManager::profileFor()`.

A second mangle rule pair covers a wide UDP range
(`5000-9999,13000-19999,21000-26999`) for **Mobile Legends and COD
Mobile**. Unlike Xbox/PSN/Steam, these games don't publish fixed
ports — they assign dynamic UDP ports per match server — so this range
is community-sourced from PH piso-wifi vendo setups rather than an
official spec. It's priority-only, not a block/allow rule, so casting
a wide net here is low-risk.

## Content Filtering
Pure MikroTik-side (`mikrotik/content_filter.rsc`, also inlined in both
`.rsc` configs): DNS is set to CleanBrowsing's Family Filter resolver
(blocks adult content, enforces Safe Search), and a NAT redirect forces
every client's DNS queries through the router regardless of what DNS
they try to use themselves. DNS-over-TLS (port 853) is blocked as a
second bypass route. **Known limitation**: DNS-over-HTTPS can't be
reliably blocked without deep packet inspection, since it rides on the
same port 443 as ordinary HTTPS — this is a limitation of DNS-based
filtering in general, not specific to this setup.

## Power Recovery
`SessionManager::saveToSPIFFS()` runs every 30 seconds
(`SESSION_SAVE_INTERVAL`), and `begin()` reloads that file on boot. A
brownout/reboot loses at most 30 seconds of session state, not the
whole session.

## Health Monitoring
`checkMikrotikConnection()` pings the RouterOS API every 30 seconds
and drives the status LED (solid = healthy, blinking = MikroTik
unreachable) and the `/api/health` endpoint the GUI polls.

## Admin Dashboard / Promo Editor / Export Tools
`admin.html` + `firmware/admin_api.h` + the `/api/admin/*` routes in
`firmware/gui_handler.h`: live overview stats, active-user list with
kick, on-device voucher generation, CSV import from
`vouchers/generator.py`, and Night Promo/idle-timeout/auto-reboot
settings. "Export Tools" in practice means `generator.py`'s CSV output
— open it in Excel/Sheets for reporting.

## Multi-Admin (Super / Staff roles)
`AdminAccount` in `admin_api.h` (username, salted MD5 password hash,
`role` — see Salted Password Hashing below). `GUIHandler::
requireAdmin(requireSuper)` checks the
`X-Admin-Username`/`X-Admin-Password` headers and, for anything with
billing or system-wide consequences (Settings, Subscriptions, other
admin accounts), also checks the role. First boot seeds a default
`admin`/`ADMIN_PASSWORD_HASH` super account automatically, so upgrading
firmware never locks out an existing deployment.
`AdminAPI::setAdminAccountActive()` refuses to disable the last active
super admin — otherwise a shop could lock itself out of its own
settings. The GUI hides super-only tabs for a staff login
(`script.js`'s `applyRoleVisibility()`), but that's just UX — the
server enforces it either way.

## Salted Password Hashing
Admin and subscriber passwords are stored as `MD5(salt + password)`,
with a random per-account `salt` (`AdminAPI::randomSalt()`, sourced
from `RANDOM_REG32` — the ESP8266's hardware RNG register, not
Arduino's seeded `random()`) generated at account-creation time.
Salting defeats precomputed rainbow-table attacks against a stolen
`admins.json`/`subscribers.json`; MD5 itself is still a fast hash (not
a proper password KDF like bcrypt/argon2), so this is a real but
partial hardening, not a complete fix. Accounts created **before**
this existed have `salt = ""`, and `md5Hex("", password)` is
mathematically identical to the old unsalted `md5Hex(password)` — so
every existing stored hash (including the default `admin`/
`ADMIN_PASSWORD_HASH` seeded on first boot) keeps working unchanged,
no migration or forced password reset needed. Only *new* accounts
(added via the Admins/Subscriptions tab after upgrading) get a real
salt going forward.

## Login Brute-Force Protection
A small fixed-size per-IP table (`GUIHandler`'s `LoginAttempt _attempts[
LOGIN_ATTEMPT_TABLE_SIZE]`, `config.h`) shared by `/api/login` (voucher/
subscriber) and admin auth (`requireAdmin()`): after `LOGIN_MAX_FAILURES`
(8) failed attempts from the same source IP within `LOGIN_LOCKOUT_MS`
(1 minute), further attempts from that IP get `429 too_many_attempts`
immediately — credentials aren't even checked, so a locked-out client
can't keep guessing. The lockout self-clears once the window passes
with no new failures, no admin action needed. A subscriber-login check
failing (i.e. the code just isn't a subscriber username) is **not**
counted as a failure — that's the normal case for every plain voucher
login; only a code that fails both the subscriber AND voucher checks
counts as a genuine wrong guess. The table is intentionally small and
in-RAM only (nothing survives a reboot) — its job is slowing down an
active guessing script, not maintaining a permanent ban list.

## Activity & Device Log
`AdminAPI::logEvent()` appends to an in-RAM rolling log
(`MAX_ACTIVITY_LOG_ENTRIES`, default 300 — oldest entries drop first)
covering coin insertions, voucher/subscriber logins, extend/disconnect,
admin actions (who changed what), and system events (boot, MikroTik
connection lost/restored). It's flushed to `activity.json` only when
dirty, piggybacked on the existing 30s session-save interval — not on
every single event, since that would wear the flash fast for a busy
coin slot. View it on the Logs tab (either role can).

## Sales Inventory
`AdminAPI::rollDailyCountersIfNeeded()` — already responsible for
resetting the "today" counters at midnight — now also pushes the
completed day into a rolling `DailySalesEntry` history
(`MAX_SALES_HISTORY_DAYS`, default 90) before resetting. Revenue is
tracked separately per payment method (`coinRevenueToday`/
`voucherRevenueToday`/`subscriptionRevenueToday`) so the Sales tab can
show a breakdown, not just one number. Subscriptions are billed
informally (no payment gateway), so their revenue comes from whatever
the admin types into the "Amount Collected" field when adding or
renewing one.

## Rebrandable Portal (no-code)
`AdminAPI::brandName`/`brandColor`, editable from Settings (super
only) and served publicly (no auth) via `GET /api/branding`, since the
login/status pages need it before anyone's authenticated. `script.js`'s
`applyBranding()` fetches it on every page load and sets the
`--accent`/`--accent-dark` CSS custom properties (see `style.css`'s
`:root`) plus the `#brandTitle` element and browser tab title. Changing
it in Settings applies live, no reload needed. Logo customization is
intentionally **not** part of this — it stays the manual SVG/PNG edit
documented in `docs/06-gui-customize.md`, since storing an uploaded
image would compete with vouchers/subscribers/logs for the NodeMCU's
limited SPIFFS space.

## Coin Slot (pulse counting)
`processCoinSlot()` in the `.ino` counts pulses from a single-line,
proportional-pulse coin acceptor (pin set in Settings → Coin Slot, default D5) — see
[11-coin-acceptor-wiring.md](11-coin-acceptor-wiring.md) for wiring and
how to program the acceptor so pulse count matches peso value.

Pricing draws from the **same Settings → Rate Profiles table vouchers
use** (see below) — a ₱5 coin and a ₱5 voucher always grant identical
minutes/data/speed. Each physical coin-drop burst's exact peso value
(pulse count) is looked up via `AdminAPI::profileByPeso()` and that
profile's minutes/data are credited directly, additively — not a flat
per-peso rate, so a ₱5 coin can be worth disproportionately more than
five ₱1 coins if the admin sets it up that way. A denomination with no
matching Rate Profile is logged (`coin_unmatched`) and credited
nothing rather than guessed at — define a profile for every real coin
value the acceptor takes.

Two defensive checks guard against noise and malfunctions rather than
crediting garbage as money: `onCoinPulse()` ignores any pulse landing
faster than `MIN_PULSE_INTERVAL_US` after the last one (contact bounce,
not a real coin), and `processCoinSlot()` discards (and logs as
`coin_anomaly`) any single burst larger than `MAX_PLAUSIBLE_PULSES`
rather than crediting it. The debounce itself is driven by a
timestamp the ISR updates on every accepted pulse
(`lastPulseMs`) — comparing against a self-set timestamp instead, as
an earlier version did, meant the wait fired on almost the first pulse
of every coin rather than after the whole burst finished.

## Rate Profiles: Unified Coin/Voucher Pricing & Validity
Settings → Rate Profiles is one admin-sized table
(`AdminAPI::rateProfiles`, a `std::vector<RateProfile>` — not fixed at
3 anymore) of `{pesoAmount, minutes, dataMb, validityMinutes,
speedProfile}` entries. It's the live source of truth for BOTH
on-device `generateVouchers(pesoAmount, count)` and the coin slot
(`AdminAPI::profileByPeso()`, see above) — one price list, not two.
Changing a profile only affects *newly generated* vouchers/coin
sessions; anything already printed or provisioned keeps whatever was
baked into its `VoucherRecord`/`SessionEntry` at the time — Settings
changes are never retroactive. Existing devices upgrading from before
Rate Profiles existed auto-migrate: a missing `rateProfiles` key in
`config.json` seeds 3 entries from the old Tier1/2/3 constants
(`AdminAPI::seedDefaultRateProfiles()`).

Each profile's `validityMinutes` (0 = never expires) does double duty:
for a voucher, it's the shelf-life before first redemption —
`AdminAPI::redeemVoucher()` rejects an expired-but-unused voucher the
same generic way it rejects an already-used one, so a customer can't
tell the two apart by the error message. For an in-progress session
(coin or voucher), the SAME number becomes `SessionEntry.
pauseWindowMinutes` — how long it may sit paused before the remaining
time/data is forfeited (`SessionManager::isPausedTooLong()`), reusing
the pause-tracking `session.h` already had; subscriptions and other
profile-less sessions fall back to the fixed `MAX_PAUSE_MINUTES`
constant. `vouchers/generator.py` reads the same rates from a shared
`vouchers/rate_profiles.json` — export the live table from the admin
dashboard's Settings tab (or Import one back in) so the on-device
table and printed-batch generator never drift apart.

Settings → Rate Profiles also has Export/Import buttons
(`GET`/`POST /api/admin/rate-profiles`) — Export downloads the current
table as JSON, Import replaces it wholesale from a file (not a merge).

## Speed Profiles (bandwidth)
Settings → Speed Profiles is an admin-sized list (up to
`MAX_SPEED_PROFILES` = 8) of `{id, name, downMbps, upMbps}` — e.g.
"Piso 2M" = 2/1 Mbps. Every Rate Profile and every subscriber picks one
by id from a dropdown. Ids `1`/`2`/`3` map to the MikroTik profiles the
desktop app's Config creates (`hs-default/hs-gaming/hs-high` and
`pppoe-default/gaming/high`); any other id is created on the router as
`zx-speed-<id>` (hotspot) / `zx-pppoe-<id>` (PPPoE) when Settings is saved
(`GUIHandler::pushSpeedProfiles()` → `MikrotikAPI::ensureHotspotUserProfile()`/
`ensurePppProfile()`; mapping in `QoSManager::hotspotProfile()`).

Speed is **not** baked in at session-creation time: a save pushes the
new `rx/tx` rate-limit (`AdminAPI::rateLimitStringFor()`) live, so it
applies immediately to users already connected on that profile. If the
router is unreachable at save time the values still save on the NodeMCU
(`mikrotikPushed: false`, shown as a warning under the Save button) and
are pushed on the next successful save. Devices upgrading from the old
three fixed tiers are migrated automatically
(`AdminAPI::seedDefaultSpeedProfiles()`). Settings validates the whole
request before saving anything: a Rate Profile pointing at a speed id
that no longer exists is rejected, not silently saved.

`GET /api/admin/tier-info` returns live labels for the dropdowns
(Vouchers, Subscriptions) and is open to staff accounts too, since staff
can generate vouchers and need accurate current pricing.

## Coin Session Flow (Insert Coin)
`firmware/coin_slot.h` (`CoinSlot`). The customer taps **Insert Coin** →
`POST /api/coin/start` reserves the slot for that device (token + MAC)
and switches the relay on → each coin is credited against its own exact
Rate Profile and shown live (`/api/coin/status` polling, ₱ + time) →
**Done - Connect** (`/api/coin/done`) provisions the session, or extends
the current one when started from the status page's **Add Time
(Coins)**. Safety nets: one reservation at a time (others get "please
wait"); no coin within 60 s cancels the reservation; 45 s after the last
coin it finishes automatically so paid time is never lost; a coin that
arrives with no reservation is held as an "orphan" for 60 s and claimed
by the next Insert Coin. Coin revenue is written to `/today.json` on
every sale so a power cut doesn't lose the day's total.

Pins are configurable without reflashing: Settings → Coin Slot → coin
signal pin (D1/D2/D5/D6/D7 — the interrupt-capable ones), relay pin (or
none), relay active HIGH/LOW, and pesos per pulse. Applied on Save
(`onCoinPinsChanged` → `applyCoinPins()` re-attaches the interrupt).

## Portal Price List & Announcement
`GET /api/branding` (unauthenticated — the customer isn't logged in yet)
returns the announcement text (Settings → Announcement, up to 300 chars)
and the rate table `[{peso, minutes, dataMb, validityMinutes}]`. The login
page renders it as a table with time as `01d:00H:00M` (`formatDHM()` in
`script.js`); the announcement shows on both login and status pages.

## Voucher Print Sheet
Vouchers → choose price + count → **Generate** creates them on the
NodeMCU, then opens `print.html` with the batch (passed via
`localStorage` key `zxheifi_print_batch`): one card per voucher with a
QR code (`qrcode.min.js`), code, ₱, time, data and a "Valid until" date.
**Print** or **Save as PDF** from the browser's print dialog. A voucher
counts as a sale when it's redeemed, not when printed.

## Sales Range + CSV
Sales shows per-day coin / voucher / subscription totals with a date
range filter; **Export CSV** downloads the filtered rows for Excel.
Today's counters persist in `/today.json` and roll over at midnight once
the clock is synced (from the router, or from the admin's browser via
the `X-Client-Time` header).

## Open (piso-style) WiFi
The desktop app's WiFi password field defaults to blank → the hAP lite's
`wlan1` uses the `zxheifi-open` security profile (no password), like
every piso WiFi: payment happens on the login page, and a password would
only leak through phones' "Share WiFi" QR. The NodeMCU firmware scans
before joining, so an open network is joined without a stale saved
password.

## Version Checks
Firmware, portal and desktop app are versioned together (1.0.0).
`/api/health` reports `fw`; the admin page compares it with
`REQUIRED_FIRMWARE` (`versionAtLeast()` in `script.js`) and shows a red
banner — and refuses to Save — when the NodeMCU is older. Each page also
checks that the `script.js` it loaded matches its own version; on a
mismatch (browser cache) it refetches once with `cache: 'reload'` and
reloads, so a phone never runs new HTML with old JavaScript.

## Voucher Ticket Design
`vouchers/generator.py --brand-name "..." --accent-color "#rrggbb"
--footer-text "..."` customize the printed ticket's look without
touching `print_template.html` — these flags are the intentionally
simple alternative to storing branding on the NodeMCU and syncing it
to the offline Python tool, which would need a whole new sync
mechanism for little benefit. Accent color mainly matters for the
on-screen preview/PDF export/color printers; most thermal printers are
monochrome regardless of what's requested.
