# Vendos Framework + Sub Vendo — Design (v2, part 1 of 4)

- **Date:** 2026-09-26
- **Status:** approved in brainstorming. Awaiting the owner's review of this document.
- **Branch:** `v2` (folder `ZxheiFi-v2`). v1.0.0 on `main` is not touched.
- **Source:** [13-v2-masterplan.md](../../13-v2-masterplan.md), Theme 1.

## Buod (Tagalog)

Unang bahagi ng v2: ang **Vendos framework** at ang **Sub Vendo**.

- **Ang sub vendo ay isang dagdag na coin box** (NodeMCU + coin acceptor) na nakakabit sa WiFi ng isang AP sa ibang lugar.
- **Taga-bilang lang ito ng barya.** Ang presyo, oras at sales ay nasa main unit (disenyong "A").
- **Sariling reservation ang bawat box**, kaya puwedeng sabay-sabay ang mga customer sa magkaibang box.
- **Pagpili ng box:** i-scan ng customer ang QR sticker ng box, o pumili sa listahan.
- **Kaligtasan ng pera:**
  - may pirma (HMAC) ang bawat mensahe;
  - may numero ang bawat coin, kaya walang doble;
  - sarado ang slot kapag offline;
  - ang coin na naipit habang offline ay ipinapadala pagbalik ng koneksyon.
- **Admin:** may "Vendos" tab para sa status, kita bawat box, coin box na may "Collected", QR sticker, at optional na commission %.
- **Limitasyon:** NodeMCU bilang main = hanggang 3 sub vendo. ESP32 = hanggang 10, sa ika-4 na bahagi ng v2.
- **Hindi kasama rito:** charging station, ESP32 build, backup/restore, ESP-NOW, GCash.

## 1. Goals and non-goals

**Goals**
1. One main unit sells hotspot time from several coin boxes ("vendos"): its own slot, plus up to N sub vendos on the same network.
2. Every peso lands exactly once: no double credit, no silent loss, no spoofed coins.
3. Owner visibility per vendo:
   - online status
   - today's income
   - coin-box contents since last collection
   - optional commission % for the location host
4. Shops with a single vendo see no change in the customer flow.
5. The design is board-agnostic. It works on an ESP8266 main (limit 3 subs) and is ready for an ESP32 main (limit 10, delivered in part 4).

**Non-goals (later specs)**
- Charging station (part 2 — reuses this framework).
- ESP32 build of the main unit and backup/restore (part 4).
- ESP-NOW link, GCash/Maya, Telegram commands.

## 2. Decisions made with the owner

| Question | Decision |
|---|---|
| How to isolate v2 | Folder `ZxheiFi-v2` = git worktree on branch `v2` of the same repo |
| Where sub vendos go | At a site that has its own ZxheiFi AP, so sub vendos join over WiFi |
| How a customer picks a box | QR sticker per box (`?vendo=<id>`), plus a picker that remembers the last choice |
| Revenue share | Optional commission % per vendo (default 0) |
| Main board | Owner's choice: ESP32 main + NodeMCU subs, or NodeMCU everywhere. The NodeMCU limits are explained in Admin, Guide and docs |
| Architecture | **A — brains on the main.** Subs only count coins and switch the relay |

## 3. Components

```
firmware/                 main unit (existing), gains:
  vendo_registry.h        list of vendos, keys, counters, box totals, collections
  vendo_api.h             /api/vendo/* (signed, for subs) + /api/admin/vendos*
  coin_slot.h             one reservation per vendo (was: one global slot)
common/
  zx_protocol.h           shared by main + sub: HMAC-SHA256, key derivation,
                          canonical signing, constants (timings, limits)
subvendo/
  subvendo.ino            sub vendo firmware (ESP8266; ESP32-compatible code)
  sub_setup.h             Setup Wizard AP "ZxheiFi-Sub-Setup"
  coin_queue.h            persistent queue of unacknowledged coins
mikrotik/gui/             login picker, Vendos tab, QR sticker page (vendo-sticker.html)
desktop-app/              Flash tab: firmware choice (Main unit / Sub Vendo)
tools/sub_sim.py          simulated sub vendo speaking the real protocol
platformio.ini (root)     envs: main_esp8266, sub_esp8266 (main_esp32 added in part 4),
                          build flag -I common
```

`common/` makes the protocol code single-source. Firmware builds move to PlatformIO environments in the repo, replacing the scratchpad project. Arduino-IDE users must add `common/` to the sketch; `firmware/BUILD_INSTRUCTIONS.md` will document this.

## 4. Vendo model (main unit)

`/vendos.json` (streamed array, temp+rename writes like the other files):

| Field | Type | Notes |
|---|---|---|
| `id` | uint8 | `0` = Main (implicit, always present, not stored as a sub) |
| `name` | string ≤ 32 | Shown to customers and in Admin |
| `mac` | string | The sub's MAC, recorded at pairing |
| `key` | 32 bytes hex | Per-vendo HMAC key (see §6) |
| `commissionPct` | uint8 0–100 | Default 0 |
| `boxTotal` | uint32 ₱ | Coins since last "Collected" |
| `lastCoinSeq` | uint32 | Highest coin sequence credited (dedupe, persisted on every coin) |
| `coinPin`, `relayPin`, `relayActiveHigh`, `pesosPerPulse` | as v1 | Pushed to the sub (§7). Main's own pins stay in Settings → Coin Slot |
| `pairedAt` | epoch | |

**Runtime-only fields:**
- `lastSeenMs`
- `lastPollCounter`
- `online`, derived: the last poll was within `VENDO_OFFLINE_MS` = 10 s.

**Pending pairing codes** are kept in RAM, with at most 2 at a time. Each is valid for 15 minutes, single-use, and lost on reboot, which is acceptable.

`/collections.json` is a rolling list of 50 entries: `{vendoId, amount, admin, at}`.

**Limits:**
- `MAX_SUB_VENDOS` is 3 on `ESP8266` and 10 on `ESP32` (a `config.h` `#ifdef`).
- Once the limit is reached, Add is refused with `vendo_limit` and a board-specific explanation.

## 5. Coin slot changes (main)

`CoinSlot` keeps a small array of `Slot` structs, one per vendo (index = vendo id). Each `Slot` holds the v1 reservation state:
- token and MAC
- extend session
- credit
- idle and no-coin timers
- orphan
- done result

All v1 rules apply per slot, unchanged:
- exact-peso Rate Profile per coin
- 45 s auto-finish
- 60 s no-coin cancel
- 60 s orphan window
- 10 min result keep
- top-up of a running session

- **Slot 0** drives the local relay pin exactly as v1 does.
- **Slot k > 0** has no local pin. Its "relay on" state is simply `reserved`, reported to sub k on its next poll.

`POST /api/coin/start` takes a new optional `vendo` field (default `0`). It returns these new errors:
- `vendo_unknown` (404)
- `vendo_offline` (409): the sub hasn't polled within 10 s. The portal says so and suggests another box.

The existing `coin_slot_busy` error now applies per vendo.

**Late coins.** A coin from sub k can arrive after its reservation closed, e.g. a coin queued while the WiFi was down. It is handled in this order:
1. **Result still held:** if slot k still holds a done-result, that is within `COIN_RESULT_KEEP_MS` (10 min). The coin is added to that result's session (extend), and logged `coin_late_extended`.
2. **Otherwise:** it becomes slot k's orphan, claimed by the next Insert Coin on that box within 60 s.
3. **Unclaimed after that:** it is logged `coin_late_unclaimed` with the amount. It still counts in sales and in `boxTotal` (the cash is in the box), so the owner can compensate.

Money is never dropped silently.

## 6. Protocol (main ↔ sub)

All messages are HTTP POST with a JSON body to the main (`http://nodemcu.zxheifi.lan`). The host is in the hotspot walled garden, so subs need no MikroTik changes and no bypass. Each message is signed: header `X-ZX-Sig: hex(HMAC-SHA256(key, rawBody))`.

### 6.1 Pairing (no secret ever crosses the air)

1. **Admin issues a code.** Admin → Vendos → Add Vendo (name) → the main creates a pending code: 12 characters from the unambiguous alphabet, shown as `XXXX-XXXX-XXXX` (≈60 bits).
2. **The owner enters it.** The code goes into the sub's Setup Wizard, along with the WiFi SSID/password and the main host (default `nodemcu.zxheifi.lan`).
3. **Both sides derive a bootstrap key:** `K0 = HMAC(normalize(code), "zxheifi-pair-v1")`. Normalize means uppercase with the dashes removed.
4. **The sub asks to pair:** `POST /api/vendo/pair {mac, nonce}`, signed with `K0`.
5. **The main finds the code.** It tries each pending code's `K0`, at most 2 of them. With no match it returns 401, and failed attempts are rate-limited (5 per minute).
6. **The main replies** with `{vendoId, name, config}`, signed with `K0`. The sub verifies this signature, so a rogue "main" can't pair it.
7. **Both derive the vendo key:** `K = HMAC(K0, "key|" + vendoId + "|" + mac + "|" + nonce)`. The main stores `K` and consumes the code.

A passive sniffer on the open WiFi sees only signed messages. Recovering the key needs an offline brute force of about 60 bits within the 15-minute code lifetime, which is out of reach for the threat model (neighbours wanting free WiFi).

### 6.2 Poll (liveness + relay command)

Sub → `POST /api/vendo/poll {v: id, n: counter}`
- The counter increases on every message and is kept in RAM on both sides.
- After a main reboot, the main accepts the first counter it sees. A replayed poll only fakes "online" briefly and grants nothing.

The main replies with a signed `{relay: bool, beep: 0|1, cfgVer, name}`.

**Cadence:**
- every **1 s** while the slot is reserved;
- every **2 s** otherwise.

So the relay opens within 2 s of Insert Coin. The response to a poll tells the sub which cadence to use.

**Fail-closed:** the sub switches its relay OFF whenever it has had no valid reply for `SUB_FAILCLOSED_MS` = 3 s.

`cfgVer` changes when Admin edits that vendo's pins, name or pulse value. The sub then calls `POST /api/vendo/config` (signed) and applies the new values without a reboot.

### 6.3 Coin

Sub → `POST /api/vendo/coin {v, n, seq, peso}`
- `seq` is a per-vendo coin sequence, persisted on the sub.
- The sub counts pulses with the v1 debounce and plausibility rules (`MIN_PULSE_INTERVAL_US`, `MAX_PLAUSIBLE_PULSES`, burst gap).

The main does:
- if `seq <= lastCoinSeq`, reply `{ok: true, dup: true}` (already credited — the idempotent resend path);
- otherwise credit slot `v` (or the late-coin path of §5), then update `lastCoinSeq`, `boxTotal` and today's per-vendo sales. It persists before replying `{ok: true}`.

On the sub, every coin goes into `coin_queue.h` first: a LittleFS file with up to 20 entries. The entry is removed only when a signed `ok` comes back, and resent every 2 s until then. If the queue is full, the relay stays off, so no more coins are accepted, and the LED shows an error.

### 6.4 Sub states and LED

| State | LED | Relay |
|---|---|---|
| Not paired | double blink | off, Setup Wizard AP up |
| Connecting / main unreachable | slow blink | off |
| Online, idle | solid | off |
| Reserved (customer inserting) | fast blink | on |
| Queue full / error | triple blink | off |

The buzzer beeps once per accepted coin. Holding FLASH during the boot window reopens the wizard, as in v1.

## 7. Admin and portal

### Admin → Vendos tab (new)
- A table with these columns:
  - name
  - online / "offline N min"
  - today ₱
  - box ₱
  - commission %
  - actions: Collected, QR sticker, Rename, Pins, Re-pair, Remove
- Main is always row 1, with no Remove / Re-pair / Pins; its pins stay in Settings → Coin Slot.
- **Add Vendo:** shows the pairing code with a 15-minute countdown and the steps to follow.
- **Collected:** a confirm dialog with the amount. It appends to `/collections.json`, logs a `vendo_collected` event and zeroes `boxTotal`. Collections can be listed.
- **Re-pair / Remove:** the old key is invalidated immediately.
- **Roles:** staff can view and press Collected. Super admin only for Add, Remove, Re-pair, Rename, Pins and commission.
- **Board limit banner:** "NodeMCU main: up to 3 sub vendos. For more, or for upcoming features (Telegram bot, GCash), use an ESP32 as the main unit."

### QR sticker page (`vendo-sticker.html`)
A printable card with:
- the vendo name
- a QR for `http://10.0.0.1/login?vendo=<id>`
- two steps: "1. Connect to the WiFi · 2. Scan this QR"
- the same styling as the voucher print sheet

### Login page
- The picker appears **only when at least one sub vendo exists**.
- The choice is preselected from `?vendo=`, then from `localStorage` (last used), then Main.
- Offline vendos are shown disabled with "(offline)".
- `/api/branding` returns `vendos: [{id, name, online}]`.

### Sales
- **Storage:**
  - `DailySalesEntry` and `/today.json` gain `byVendo: [{id, peso}]`, for coin revenue only.
  - Vouchers and subscriptions stay as today and are attributed to Main in reports.
- **Sales tab:** gets a vendo filter. When any commission is above 0, it adds the columns: Total · Host share · Owner share.
- **CSV:** one row per date × vendo: `date, vendo, coins, commission_pct, host_share, owner_share`.

### Telegram
If Telegram is enabled, the main sends "Vendo *Tindahan* offline for 5 min" and "... back online". It sends one message per transition, no spam.

## 8. Setup Companion

- **Flash tab:** gets a "Device type" choice: **Main unit** (`zxheifi_firmware.bin`) or **Sub Vendo** (`zxheifi_subvendo.bin`). Both files are bundled.
- **Guide (EN/TL):**
  - a new "Sub Vendo" section: flash, Add Vendo, the wizard, the QR sticker, collections and commission;
  - the board-limit explanation.
- `test_configurator` needs no changes, because there are no MikroTik changes.

## 9. Upgrade from v1

1. Flash the v2 main firmware.
2. On first boot, the absence of `/vendos.json` means "Main only".
3. Existing sales and `today.json` entries without `byVendo` are read as 100% Main.
4. Settings, vouchers, sessions and admins are unchanged.

v2 firmware version: `2.0.0-dev` until release.
- `versionAtLeast` treats `-dev` as older than the release.
- `REQUIRED_FIRMWARE` in the GUI moves with it.

## 10. Error handling summary

| Situation | Behaviour |
|---|---|
| Insert Coin on an offline sub | Refused (`vendo_offline`), portal suggests another box |
| Sub drops offline mid-reservation | Credit already received is kept; 45 s auto-finish as in v1 |
| Coin arrives after the reservation closed | §5 late-coin rules: extend, orphan, or logged unclaimed — never dropped |
| Duplicate coin message (retry) | Acknowledged as `dup`, not credited twice |
| Bad signature / unknown vendo / removed vendo | 401; sub shows "not paired"; repeated failures logged (rate-limited) |
| Main reboot | Registry reloads from flash; subs resume on their next poll |
| Sub reboot | Unacked coins resent from the persistent queue |
| Coin queue full on the sub | Relay stays off (accepts no coins), error LED |
| Low heap on the main | Vendo endpoints use small fixed JSON docs; per-vendo sales lives in the existing sales files (no new per-request allocations beyond ~512 B) |

## 11. Testing

1. **Mock server** (`tools/mock_server.py`):
   - implements every new endpoint with the real HMAC scheme;
   - `tools/sub_sim.py` pairs with it and drives polls and coins, including retries, replays, an offline window and queue flushes.
2. **`tools/regression_test.py`** gains checks for:
   - pairing: success, wrong code, expired code, single use, rate limit;
   - signed poll: relay reflects the reservation; fail-closed timing is simulated by `sub_sim`;
   - coin credited once when sent twice; replayed old seq ignored; unknown vendo rejected;
   - Insert Coin on an offline vendo refused;
   - late coin → extend, then orphan, then unclaimed-logged;
   - the vendo limit (3) and its message;
   - commission math and CSV rows; Collected zeroes the box and records history;
   - staff vs super permissions on the Vendos endpoints;
   - branding lists vendos; the picker is hidden with no subs.
3. **Firmware:** PlatformIO builds `main_esp8266` and `sub_esp8266` with no new warnings. Heap is checked on the real NodeMCU with 3 simulated subs polling (target: at least 15 KB free).
4. **Browser** (mock):
   - login picker, `?vendo=`, remembered choice;
   - Vendos tab, pairing-code dialog, Collected, sticker page;
   - sales filter and CSV.
5. **Real hardware:**
   - a second NodeMCU + coin acceptor as the sub, behind the owner's AP;
   - verify that `http://10.0.0.1/login?vendo=2` keeps the query through the MikroTik captive redirect. If it doesn't, the `localStorage` choice and the picker cover it, and the sticker text is adjusted.

## 12. Risks and open items

- **Captive-portal query passthrough.** Covered by the fallback and to be verified on the real router (§11.5).
- **ESP8266 web server load.** Three subs polling every 1–2 s adds ~2–3 req/s. The server is single-threaded, so this is measured on hardware. If it's too heavy, the idle poll interval goes up to 3 s, which also slows how fast the relay opens.
- **Clock-free design.** The protocol uses counters, not timestamps, so it works before NTP sync.
- **Hardware needed from the owner:** a second NodeMCU + coin acceptor + relay + buzzer for the first sub vendo.
