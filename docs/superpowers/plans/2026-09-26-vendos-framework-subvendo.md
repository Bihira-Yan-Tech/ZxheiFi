# Vendos Framework + Sub Vendo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One ZxheiFi main unit sells hotspot time from several coin boxes. It has its own slot plus up to 3 (ESP8266) or 10 (ESP32) paired sub vendos. Every peso is counted exactly once, and the owner sees status, income, coin-box contents and commission per box.

**Architecture:** "Brains on the main". A sub vendo only counts pulses and switches its relay; the main owns reservations, prices, sessions and sales. Subs talk to the main over signed HTTP:
- HMAC-SHA256 with a per-vendo key derived from a one-time pairing code;
- message counters against replay;
- coin sequence numbers for dedupe;
- reservation ids (`rid`) so a late coin reaches the right customer.

`tools/mock_server.py` mirrors every endpoint and is the test double. `tools/sub_sim.py` is a scripted sub vendo speaking the real protocol.

**Tech Stack:** ESP8266 Arduino core + ArduinoJson 6 (PlatformIO), vanilla JS portal, Python 3.12 (mock, tests, Setup Companion with CustomTkinter, PyInstaller, Inno Setup).

**Spec:** [docs/superpowers/specs/2026-09-26-vendos-framework-subvendo-design.md](../specs/2026-09-26-vendos-framework-subvendo-design.md)

## Global Constraints

**Limits and versions**
- `MAX_SUB_VENDOS` = 3 on ESP8266 and 10 on ESP32 (`#if defined(ESP32)`). `BOARD_NAME` = `"esp8266"` / `"esp32"`.
- Version: firmware, GUI, sub firmware and app are all `2.0.0-dev`. `versionAtLeast("2.0.0-dev","2.0.0")` is false.

**Timings** (identical in `common/zx_protocol.h` and `tools/zx_protocol.py`)

| Constant | Value |
|---|---|
| Poll interval, idle | 2000 ms |
| Poll interval, reserved | 1000 ms |
| Sub fail-closed | 3000 ms |
| Vendo offline threshold | 10000 ms |
| Pairing code TTL | 900000 ms |
| Coin resend interval | 2000 ms |
| Coin queue max | 20 |
| Max pending pairings | 2 |
| Pair-failure limit | 5 per 60000 ms |
| Offline alert after | 300000 ms |

**Protocol**
- Signature header `X-ZX-Sig` = lowercase hex HMAC-SHA256(key, exact raw body bytes).
- Pairing code: 12 characters from `23456789ABCDEFGHJKLMNPQRSTUVWXYZ`, shown as `XXXX-XXXX-XXXX`. Normalization is uppercase and keeps `[A-Z0-9]` only.
- `K0 = HMAC(normalizedCode, "zxheifi-pair-v1")`.
- `K = HMAC(K0, "key|<id>|<mac>|<nonce>")`.

**Test vectors** (both implementations must match)
- RFC 4231 #2: key `Jefe`, message `what do ya want for nothing?` → `5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843`
- RFC 4231 #6: key = 131×`0xaa`, message `Test Using Larger Than Block-Size Key - Hash Key First` → `60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54`
- `K0("k7qm-2xpa-9rtd")` = `7410214d8edef1bf3a6262cdadb1c3b7900637133ef96a7064103d33472f38a8`
- `K(K0, 1, "AA:BB:CC:DD:EE:01", "0123456789abcdef")` = `1c4c3b7ce2a175b5fd466c49ec2e5ebac349c4a8a9d1856f1d22af0d2aea77a5`
- `sign(K, {"v":1,"n":5})` = `5567192daee74eaac073c34abe60349aebfc05e2b4905d31555b3aca1845490d`

**Invariants**
- v1 behaviour of slot 0 (the main's own acceptor) must not regress: `tools/regression_test.py` stays 103/103 after every task.
- Nothing touches MikroTik configuration. `desktop-app/test_configurator.py` stays ALL PASSED.
- No new warnings in `pio run` beyond the existing SPIFFS deprecations.

## Refinements decided while planning (spec intent unchanged)

1. **Reservation id (`rid`) instead of `beep`.**
   - Every reservation gets a unique `rid`. The poll reply carries it; a sub tags each coin with the rid active when the coin went in (0 = none).
   - The main credits `rid == current` to the reservation, and `rid == doneRid` (within 10 min) to the finished session (late extend). Anything else becomes an orphan.
   - This is how the spec's late-coin rule stays correct when a new customer has already reserved the same box.
   - The sub chirps locally when its relay turns on, replacing the spec's `beep` field.
2. **Main stored as a vendo.** Main is stored in `/vendos.json` as `id 0`, which is needed for its coin-box total and commission.
3. **Expired orphans on any slot are recorded as unclaimed revenue** (logged `coin_late_unclaimed`). v1 silently dropped them.
4. **Sub message counters survive reboots.** The sub persists its counter in blocks of 1000, so after a reboot it resumes above anything the main has seen.
5. **Pairing replies carry `lastCoinSeq`.** The sub raises its own sequence to it, so a sub whose flash was erased cannot have its coins rejected as duplicates.
6. **Main's box counts at physical receipt.** Its `boxTotal` counts every coin as it goes in, including unmatched denominations and orphans, because the cash is in the box. Sales counts revenue when credit is delivered, or when an orphan is declared unclaimed.

## File map

| File | Responsibility |
|---|---|
| `common/zx_protocol.h` (new) | Portable SHA-256/HMAC, hex, key derivation, sign/verify, constants — shared by main + sub |
| `common/selftest/` (new) | On-device check of the C++ protocol against the vectors (no host compiler on this PC) |
| `tools/zx_protocol.py` (new) + `tools/test_zx_protocol.py` (new) | Python reference implementation + vector tests |
| `firmware/platformio.ini` (new) | `main_esp8266` env, `-I ../common` |
| `firmware/config.h` | `MAX_SUB_VENDOS`, `BOARD_NAME`, `ZX_RANDOM32`, file names, version `2.0.0-dev` |
| `firmware/admin_api.h` | Per-vendo coin revenue (`VendoPeso`, `addCoinRevenue`, today/history persistence) |
| `firmware/vendo_registry.h` (new) | Vendos, keys, counters, pending codes, box totals, collections, persistence |
| `firmware/coin_slot.h` | One slot per vendo, rids, remote coins, late extend, unclaimed orphans |
| `firmware/vendo_api.h` (new) | `/api/vendo/*` (signed) + `/api/admin/vendos*` + offline alerts |
| `firmware/gui_handler.h` | `X-ZX-Sig` header, public reply/auth/stream wrappers, `vendo` on coin/start, `vendos` in branding, `board` in health, `byVendo` in sales |
| `firmware/nodemcu_firmware.ino` | Wiring |
| `subvendo/*` (new) | Sub firmware: `sub_config.h`, `sub_store.h`, `coin_queue.h`, `sub_setup.h`, `subvendo.ino`, `platformio.ini`, `zxheifi_subvendo.bin` |
| `tools/mock_server.py` | Test clock, slots, vendos, protocol, sales `byVendo` |
| `tools/sub_sim.py` (new) | Scriptable sub vendo (library + CLI for real hardware) |
| `tools/vendo_test.py` (new) | Contract tests for everything above (fresh mock per test) |
| `mikrotik/gui/*` | Picker (login/status), `versionAtLeast` -dev, Vendos tab, `vendo-sticker.html`, sales vendo view + per-vendo CSV |
| `desktop-app/*` | Device type (Main / Sub Vendo) in Flash tab, bundle both bins, Guide EN/TL, version |
| `docs/*` | 14-sub-vendo.md, changelog, README, BUILD_INSTRUCTIONS, masterplan status |

---

### Task 1: Protocol reference (Python) + vector tests

**Files:**
- Create: `tools/zx_protocol.py`
- Create: `tools/test_zx_protocol.py`

**Interfaces — Produces:**
- `hmac256(key: bytes, msg: bytes) -> bytes`
- `normalize_code(code: str) -> str`
- `pair_key(code: str) -> bytes`
- `vendo_key(k0: bytes, vid: int, mac: str, nonce: str) -> bytes`
- `sign(key: bytes, body: bytes) -> str`
- `verify(key: bytes, body: bytes, sig_hex: str) -> bool`
- `new_pair_code() -> str`
- Constants `SIG_HEADER`, `POLL_IDLE_MS`, `POLL_ACTIVE_MS`, `SUB_FAILCLOSED_MS`, `VENDO_OFFLINE_MS`, `PAIR_CODE_TTL_MS`, `COIN_RESEND_MS`, `COIN_QUEUE_MAX`, `MAX_PENDING_PAIRS`, `PAIR_FAIL_LIMIT`, `PAIR_FAIL_WINDOW_MS`, `OFFLINE_ALERT_MS`, `CODE_ALPHABET`.

- [ ] Write `tools/test_zx_protocol.py` (unittest). It asserts:
  - the five vectors from Global Constraints;
  - `normalize_code(" k7qm-2xpa 9rtd ") == "K7QM2XPA9RTD"`;
  - `verify` rejects a tampered body, a wrong-length signature and a non-hex signature;
  - `new_pair_code()` matches `^[2-9A-HJ-NP-Z]{4}-[2-9A-HJ-NP-Z]{4}-[2-9A-HJ-NP-Z]{4}$`.
- [ ] Run `python tools/test_zx_protocol.py`. Expected: ImportError (FAIL).
- [ ] Implement `tools/zx_protocol.py`: `hmac` + `hashlib`, `compare_digest` in `verify`, `secrets.choice` for codes.
- [ ] Run the tests. Expected: all pass.
- [ ] Commit `feat(v2): protocol reference implementation + vectors`.

### Task 2: C++ protocol header, on-device self-test, PlatformIO projects

**Files:**
- Create: `common/zx_protocol.h`
- Create: `common/selftest/platformio.ini`
- Create: `common/selftest/selftest.ino`
- Create: `firmware/platformio.ini`
- Create: `tools/serial_watch.py`

**Interfaces — Produces** (namespace `zx`):
- `class Sha256 { init(); update(const uint8_t*, size_t); final(uint8_t[32]); }`
- `sha256(...)`
- `hmacSha256(key, keyLen, msg, msgLen, out[32])`
- `toHex(...)`
- `fromHex(const char*, uint8_t*, size_t) -> bool`
- `equalConstTime(...)`

Under `ARDUINO`:
- `hexOf(const uint8_t*, size_t) -> String`
- `normalizeCode(const String&) -> String`
- `pairKey(const String& code, uint8_t k0[32])`
- `vendoKey(const uint8_t k0[32], uint8_t id, const String& mac, const String& nonce, uint8_t out[32])`
- `sign(const uint8_t key[32], const String& body) -> String`
- `verify(const uint8_t key[32], const String& body, const String& sigHex) -> bool`

Plus the `ZX_*` constants.

The SHA-256 is a line-for-line port of the algorithm already validated against `hashlib` during planning. The constants were checked by deriving them from the cube roots of the first 64 primes.

- [ ] Write `selftest.ino`. It checks all five vectors plus `verify` true/false. It prints `SELFTEST PASSED`/`FAILED` every 2 s so a late serial reader still sees it.
- [ ] Write `zx_protocol.h`.
- [ ] Build the self-test: `pio run -d common/selftest`. Expected: SUCCESS.
- [ ] Flash to the NodeMCU on COM5: `pio run -d common/selftest -t upload --upload-port COM5`.
  - The owner authorized implementation + testing; this unit is their test rig.
  - Only the app partition is overwritten; SPIFFS data stays.
- [ ] Read the result: `python tools/serial_watch.py COM5 --expect "SELFTEST PASSED" --timeout 20`. Expected: exit 0.
- [ ] Restore the v1 main firmware on COM5: `python -m esptool --port COM5 write_flash 0x0 firmware/zxheifi_firmware.bin`.
- [ ] Add `firmware/platformio.ini`:
  - `src_dir = .`
  - env `main_esp8266`
  - `lib_deps = bblanchon/ArduinoJson@^6.21.0`
  - `build_flags = -I ${PROJECT_DIR}/../common`
- [ ] Run `pio run -d firmware`. Expected: SUCCESS (unchanged v1 code).
- [ ] Commit `feat(v2): shared C++ protocol + on-device self-test + in-repo PlatformIO`.

### Task 3: Mock — test clock + one coin slot per vendo (no behaviour change)

**Files:**
- Modify: `tools/mock_server.py` (coin section + `/api/coin/*` + `/dev/coin` handlers)
- Create: `tools/vendo_test.py` (harness)

**Interfaces — Produces:**
- `now()` and `CLOCK`
- `new_slot()`
- `slots: dict[int, dict]`
- `RID`
- `coin_clear_credit(s)`, `coin_release(s)`, `coin_limit(s)`
- `coin_credit(s, peso) -> bool`
- `add_coin_revenue(vid, peso)`
- `coin_orphan(vid, peso)`, `coin_add(vid, peso)`
- `coin_finish(vid) -> (code, extended)`
- `coin_unclaimed(vid, peso)`
- `coin_tick()`
- `coin_done_result(token) -> (code, extended) | None`
- `slot_for_token(token) -> (vid, slot) | (None, None)`
- `POST /dev/clock {"advance": sec}`
- `admin_stats["coinByVendo"]`

**Harness (`tools/vendo_test.py`):**
- Starts a fresh mock per test on port 8098.
- Provides `request(method, path, body=None, headers=None, raw=None)`, `admin(user)`, `advance(sec)` and `check(name, cond, detail)`.
- Exits 1 on any failure.

- [ ] Write the test `test_clock_releases_unpaid_reservation`:
  1. start → 200;
  2. a second MAC → 409 `coin_slot_busy`;
  3. advance 61 → start → 200.
- [ ] Run `python tools/vendo_test.py`. Expected: FAIL (no `/dev/clock`).
- [ ] Refactor the mock coin section to slots (vendo 0 = v1 slot) and add `/dev/clock`:
  - `coin/start` accepts optional `vendo` (unknown → 404 `vendo_unknown`).
  - Status, done and cancel find the slot by token.
- [ ] Run the tests. `vendo_test` passes and `python tools/regression_test.py` gives 103 passed.
- [ ] Commit `refactor(mock): per-vendo coin slots + test clock`.

### Task 4: Mock — vendo registry + admin endpoints

**Files:**
- Modify: `tools/mock_server.py`
- Test: `tools/vendo_test.py`

**Interfaces — Produces:**
- `vendos` (0 = Main), `pending_pairs`, `collections`, `pair_failures`
- `new_sub_vendo(vid, name)`
- `vendo_online(v)`
- `expire_pending()`
- `free_vendo_id()`
- `add_pending(name, target_id=0) -> (code, err)`
- `vendo_view(v)`, `vendo_config(v)`
- `clean_name(raw)`

**Endpoints:**

| Method + path | Role | Behaviour |
|---|---|---|
| `GET /api/admin/vendos` | staff+ | Returns `{board, limit, vendos[], pending[]}`; `pending` is super-only, else `[]` |
| `POST /api/admin/vendos/add {name}` | super | `{code, expiresInSec}`, or 409 `vendo_limit` / `too_many_pending`, or 400 `bad_name` |
| `POST /api/admin/vendos/update {id, name?, commissionPct?, coinPin?, relayPin?, relayActiveHigh?, pesosPerPulse?}` | super | Validates everything first; `cfgVer++` on config change; Main accepts name/commission only |
| `POST /api/admin/vendos/collected {id}` | staff+ | Returns `{collected}`, zeroes the box, appends to collections (max 50) |
| `GET /api/admin/vendos/collections` | staff+ | Newest first |
| `POST /api/admin/vendos/remove {id}` | super | Main → 400 `cannot_remove_main` |
| `POST /api/admin/vendos/repair {id}` | super | Invalidates the key, returns a new code bound to the same id |

`/dev/coin` also adds to the box.

**Tests:**
- default list is Main only, esp8266, limit 3;
- staff add → 403;
- super add → code format; the code is visible to super but not to staff;
- 3rd pending → 409 `too_many_pending`;
- pending expires after advance 901;
- update Main name/commission; commission 101 → 400; empty name → 400;
- `/dev/coin` 10 → box 10 → staff collected 10 → box 0; the collections entry has admin `cashier1`;
- remove Main → 400.

- [ ] Write the tests.
- [ ] Run them: they fail.
- [ ] Implement.
- [ ] Run the tests (plus regression): they pass.
- [ ] Commit `feat(mock): vendo registry + admin endpoints`.

### Task 5: Mock — device protocol + `tools/sub_sim.py`

**Files:**
- Modify: `tools/mock_server.py` (`_read_raw`, `_signed`, `_auth_vendo`, routes)
- Create: `tools/sub_sim.py`
- Test: `tools/vendo_test.py`

**Device endpoints** (all signed both ways; every reply echoes `n`, or `nonce` for pair):

`POST /api/vendo/pair {mac, nonce}`, signed with K0 of a pending code.
- Rate limit: 5 failures per 60 s → 429.
- No matching code → 401 `bad_code`.
- Success replies `{vendoId, name, nonce, lastCoinSeq, config{cfgVer, name, coinPin, relayPin, relayActiveHigh, pesosPerPulse}}`, signed with K0. The key is stored as K.
- If there's no free id → 409 `vendo_limit`.

`POST /api/vendo/poll {v, n}` → `{n, relay, rid, fast, cfgVer, name}`.

`POST /api/vendo/coin {v, n, seq, peso, rid}` → `{n, ok, dup?}`.
- `seq <= lastCoinSeq` is answered as a duplicate.
- Otherwise the coin is added to the box and then `vendo_coin(vid, peso, rid)` runs (Task 5: `coin_add`).

`POST /api/vendo/config {v, n}` → `{n, ...config}`.

**Common auth for poll, coin and config:**
- unknown or unpaired vendo → 401 `unpaired`;
- bad signature → 401 `bad_sig`;
- `n` not above the last seen → 409 `replay`.

A successful auth sets `lastSeen`. `vendo_tick()` logs `vendo_offline` once, after `OFFLINE_ALERT_MS`.

**`sub_sim.py` — class `SubSim(host="localhost", port=8098, mac=...)`:**
- methods `pair(code)`, `poll()`, `drop_coin(peso)`, `flush()`, `fetch_config()` and `raw_post(path, payload, key, tamper=False)`;
- attributes `vid`, `key`, `n`, `seq`, `rid`, `relay`, `queue`;
- CLI `python tools/sub_sim.py --host 10.0.0.254 --port 80 --code XXXX-XXXX-XXXX [--coin 5 ...]` for real hardware.

**Tests:**
- Pairing:
  - pair OK, with a verified reply signature and vid 1; the list shows the sub online and paired;
  - reusing a code → 401; a wrong code 5× → 429;
  - an expired code → 401.
- Poll:
  - relay false;
  - a tampered signature → 401;
  - a replayed `n` → 409;
  - offline after advance 11, online again after the next poll.
- Coins:
  - seq 1 → ok; seq 1 again → dup; the box is 10.
- Limit: pair 3 subs, then add a 4th → 409 `vendo_limit`.
- Config: an admin pin update raises `cfgVer` in the poll reply, and config returns the new pins.
- Remove → poll 401. Re-pair → the old key gets 401; the new code re-pairs to the same id with `lastCoinSeq` 1.

- [ ] Write the tests.
- [ ] Run them: they fail.
- [ ] Implement.
- [ ] Run the tests (plus regression): they pass.
- [ ] Commit `feat(mock): signed sub-vendo protocol + sub simulator`.

### Task 6: Mock — multi-vendo coin semantics, branding, sales

**Files:**
- Modify: `tools/mock_server.py`
- Test: `tools/vendo_test.py`

**Behaviour:**
- `coin/start`: a sub that isn't online → 409 `vendo_offline`.
- Branding returns `vendos:[{id,name,online}]` (Main plus paired subs).
- `vendo_coin(vid, peso, rid)`:
  - `rid` equals the current reservation → credit it;
  - otherwise, `rid == doneRid` within `COIN_RESULT_KEEP_SEC` → `coin_late_extend` (adds the profile to the done session, adds revenue, logs `coin_late_extended`);
  - otherwise → orphan.
- Sales `today.byVendo` and history `byVendo` are `[{id, peso}]`.

**Tests:**
- branding lists Main only, then Main + the sub;
- `coin/start` vendo 9 → 404; an offline sub → 409;
- two customers at once on vendo 0 and vendo 1 → both 200;
- the poll shows relay true with rid > 0;
- a sub coin with that rid → coin status shows ₱10;
- done → `byVendo` contains `{id:1, peso:10}`;
- a late coin with the old rid extends the session by 3600 s and logs `coin_late_extended`;
- a coin with rid 0 → orphan, claimed by the next start on vendo 1;
- a coin with rid 0 left unclaimed, then advance 61 → `byVendo` increases and `coin_late_unclaimed` is logged;
- an old-rid coin during a new reservation → not credited to the new customer.

- [ ] Write the tests.
- [ ] Run them: they fail.
- [ ] Implement.
- [ ] Run the tests (plus regression): they pass.
- [ ] Commit `feat(mock): multi-vendo coin rules, branding vendos, per-vendo sales`.

### Task 7: Firmware — per-vendo coin revenue in AdminAPI

**Files:**
- Modify: `firmware/admin_api.h`
- Modify: `firmware/gui_handler.h` (`handleAdminSales`)
- Modify: `firmware/coin_slot.h` (`finish` uses `addCoinRevenue(0, …)`)

**Interfaces — Produces:**
- `struct VendoPeso { uint8_t id; uint32_t peso; }`
- `DailySalesEntry::byVendo`
- `AdminAPI::coinByVendoToday`
- `AdminAPI::addCoinRevenue(uint8_t vendoId, uint32_t peso)`: adds to `coinRevenueToday` and `byVendo`, then saves today.
- `AdminAPI::coinTodayFor(uint8_t id) -> uint32_t`

**Persistence:**
- today's file: doc size 384 → 1024;
- sales history item: 320 → 1024;
- a missing `byVendo` in old files reads as `[]`. The UI treats it as all-Main.

- [ ] Implement.
- [ ] Run `pio run -d firmware`. Expected: SUCCESS.
- [ ] Commit `feat(fw): per-vendo coin revenue`.

### Task 8: Firmware — `vendo_registry.h`

**Files:**
- Create: `firmware/vendo_registry.h`
- Modify: `firmware/config.h`

**Interfaces — Produces:**
- `struct Vendo` (fields as in spec §4, plus runtime `lastSeenMs`, `lastCounter`, `counterSeen`, `offlineAlerted`, `hasKey`)
- `struct PendingPair`
- `struct CollectionEntry`

`class VendoRegistry`:
- `begin()`
- `find(uint8_t) -> Vendo*`
- `all() -> std::vector<Vendo>&` (reserved capacity, so pointers stay valid)
- `subCount()`
- `isOnline(const Vendo&)`, `isOnline(uint8_t)`
- `addPending(name, targetId, String& code) -> String err`
- `pending()`, `pendingAt(int)`
- `matchPending(body, sigHex) -> int`
- `completePair(int idx, mac, nonce, uint8_t k0Out[32]) -> Vendo*`
- `acceptCounter(Vendo&, uint32_t n) -> bool`
- `addToBox(id, peso)`
- `collect(id, admin) -> uint32_t`
- `collections()`
- `invalidate(id)`, `remove(id)`
- `save()`

**Storage:** `/vendos.json` (the key as hex; Main as id 0) and `/collections.json` (max 50).

**`config.h` additions:**
- `MAX_SUB_VENDOS`, `BOARD_NAME`, `ZX_RANDOM32()`
- `VENDOS_FILE`, `COLLECTIONS_FILE`, `MAX_COLLECTIONS`
- `FIRMWARE_VERSION "2.0.0-dev"`

- [ ] Implement, and include it from the `.ino`.
- [ ] Run `pio run -d firmware`. Expected: SUCCESS.
- [ ] Commit `feat(fw): vendo registry`.

### Task 9: Firmware — CoinSlot per vendo + GUIHandler changes

**Files:**
- Modify: `firmware/coin_slot.h` (rewrite around `Slot _slots[MAX_SUB_VENDOS + 1]`)
- Modify: `firmware/gui_handler.h`

**Interfaces — Produces** (`CoinSlot`):
- `begin(..., VendoRegistry&)`
- `start(uint8_t vendo, mac, extend, token&, waitSec&)`: errors `vendo_unknown`, `vendo_offline`, `coin_slot_busy`
- `status(token, out)`, which adds `vendo`
- `done(token, code&, extended&)`
- `cancel(token)`
- `addCoin(uint8_t vendo, uint32_t peso)`
- `addRemoteCoin(uint8_t vendo, uint32_t peso, uint32_t rid)`
- `relayFor(uint8_t vendo, uint32_t& rid) -> bool`
- `resetSlot(uint8_t vendo)`
- `loop()`
- `applyRelayPin()`

`GUIHandler`:
- collect the `X-ZX-Sig` header;
- `setVendoRegistry(VendoRegistry&)`;
- public `replyJson`, `replyError`, `replyOk`, `authAdmin(bool)`, `streamBegin`, `streamItem`, `streamEnd`, `streamRaw`;
- coin/start reads `vendo` and maps 404/409;
- branding adds `vendos`;
- health adds `board`.

- [ ] Implement.
- [ ] Update the `.ino`: `processCoinSlot` calls `vendoRegistry.addToBox(0, peso)` and then `coinSlot.addCoin(0, peso)`.
- [ ] Run `pio run -d firmware`. Expected: SUCCESS.
- [ ] Commit `feat(fw): one coin slot per vendo with reservation ids`.

### Task 10: Firmware — `vendo_api.h` + wiring

**Files:**
- Create: `firmware/vendo_api.h`
- Modify: `firmware/nodemcu_firmware.ino`

This task implements the same endpoints and rules as mock Tasks 4–6. It adds:
- signed replies: `X-ZX-Sig` over the serialized reply body;
- the pair rate limit (a ring of 5 timestamps);
- offline alerts every 5 s in `loop()` (Telegram + log), and "back online" on the next authenticated message;
- `lastCoinSeq` and box persisted before crediting and acking;
- the collections list streamed.

- [ ] Implement.
- [ ] Wiring: create `vendoRegistry.begin()` at the start of `setupRoutes()`, then `vendoApi.begin(...)` after `guiHandler.begin`, then `vendoApi.loop()` in `loop()`.
- [ ] Run `pio run -d firmware`. Expected: SUCCESS, no new warnings. Record flash and RAM.
- [ ] Copy `firmware/.pio/build/main_esp8266/firmware.bin` → `firmware/zxheifi_firmware.bin`.
- [ ] Commit `feat(fw): sub-vendo protocol + Vendos admin API`.

### Task 11: Sub vendo firmware

**Files:**
- Create: `subvendo/platformio.ini`
- Create: `subvendo/sub_config.h`
- Create: `subvendo/sub_store.h`
- Create: `subvendo/coin_queue.h`
- Create: `subvendo/sub_setup.h`
- Create: `subvendo/subvendo.ino`
- Create: `subvendo/zxheifi_subvendo.bin`

**Behaviour** (spec §6.2–6.4 plus the refinements):

Setup and storage:
- A Setup Wizard AP `ZxheiFi-Sub-Setup` collects WiFi, main host (default `nodemcu.zxheifi.lan`) and the pairing code. It opens when unconfigured, or when FLASH is pressed during the 3 s boot window.
- State lives in LittleFS `/sub.json`; the coin queue lives in `/queue.json`.

Pairing: retried every 15 s while there's a code but no pairing.

Polling:
- every 2 s when idle, 1 s when reserved, 15 s once the main has answered 401;
- when `cfgVer` changes, the sub fetches the new config and re-applies its pins.

Coins:
- The ISR counts pulses with the v1 debounce. `burstRid` is captured at the first pulse of a burst.
- Every burst goes into the persistent queue first. Sending is immediate, with resends every 2 s until a verified `ok` arrives.

Relay: ON only if all of these hold:
- paired, and the key isn't rejected;
- the last verified reply is less than 3 s old;
- the poll says relay on;
- the queue holds fewer than `max - 2` coins.

LED patterns: double = not paired, slow = connecting, solid = online, fast = reserved, triple = queue nearly full.

Buzzer: 1 beep per coin, a chirp when the relay turns on, 3 beeps when paired.

- [ ] Implement.
- [ ] Run `pio run -d subvendo`. Expected: SUCCESS.
- [ ] Copy the bin to `subvendo/zxheifi_subvendo.bin`.
- [ ] Commit `feat(sub): sub vendo firmware`.

### Task 12: Portal — picker, version, Vendos tab, sticker, sales

**Files:**
- Modify: `mikrotik/gui/script.js`, `login.html`, `status.html`, `admin.html`, `style.css`
- Create: `mikrotik/gui/vendo-sticker.html`
- Modify: `tools/mock_server.py` (`FIRMWARE_VERSION`)

**Behaviour:**
- `escHtml()` helper.
- `versionAtLeast` handles a `-tag` pre-release.
- `ZX_GUI_VERSION`, `REQUIRED_FIRMWARE` and the HTML `var want` all become `2.0.0-dev`.

Picker (`#vendoPicker` / `#vendoSelect` on login and status):
- shown only when branding lists at least 2 vendos;
- preselected from `?vendo=`, then `localStorage.zxheifi_vendo`, then the first online vendo;
- offline options are disabled;
- the choice is saved on change.

Insert Coin:
- sends `vendo`;
- 409 `vendo_offline` → "That coin box is offline right now - please choose another one.";
- 404 → the branding is refreshed.

Admin → **Vendos** tab (staff can see it):
- the board-limit note;
- Add Vendo (super; disabled at the limit), which shows the pairing code;
- pending codes;
- a table with actions Collected / QR / Edit (inline panel) / Re-pair / Remove;
- a collections table.

`vendo-sticker.html?vendo=&name=` shows a QR for `http://<host>/login?vendo=<id>` and two steps.

Sales:
- a vendo selector (All / each vendo);
- for a single vendo, the table shows `Date · Coins · Commission % · Host share · Your share`;
- a new "Export per-vendo CSV" button with columns `date, vendo, coins, commission_pct, host_share, owner_share`.

- [ ] Implement.
- [ ] Run `node --check` on `script.js` and the inline scripts.
- [ ] Browser check on the mock (launch `zxheifi-mock`) with a paired `sub_sim` running in a background thread:
  - the picker and `?vendo=`;
  - an offline sub disabled in the picker;
  - Insert Coin at the sub → coin → Done;
  - Vendos tab: add (code shown), Collected, Edit pins, sticker page QR;
  - sales vendo view + per-vendo CSV;
  - no console errors.
- [ ] Commit `feat(gui): vendo picker, Vendos admin tab, QR stickers, per-vendo sales`.

### Task 13: Setup Companion, docs, versions

**Files:**
- Modify: `desktop-app/main.py`, `build.py`, `installer.iss`, `guide_content.py`
- Modify: `README.md`, `docs/09-changelog.md`, `docs/13-v2-masterplan.md`, `firmware/BUILD_INSTRUCTIONS.md`
- Create: `docs/14-sub-vendo.md`

**Behaviour:**

Flash tab: a "Device type" switch (Main unit / Sub Vendo).
- It sets the default `.bin`: bundled `zxheifi_firmware.bin` or `zxheifi_subvendo.bin`.
- For a Sub Vendo, the detected MAC is not pushed into Configure MikroTik's NodeMCU MAC field.
- A hint explains the Sub Vendo setup AP.

Build and versions:
- `build.py` bundles the sub bin under `firmware/`.
- `APP_VERSION`, the title and `AppVersion` become `2.0.0-dev`.

Guide: new "Sub Vendo" sections in EN + TL (same position), covering:
- flash;
- Add Vendo code;
- the wizard;
- QR sticker;
- Collected;
- commission;
- board limits (NodeMCU up to 3; ESP32 up to 10 once available);
- problems and fixes.

Docs:
- `docs/14-sub-vendo.md`: wiring, pairing, protocol summary, troubleshooting.
- Changelog entry, README feature list, BUILD_INSTRUCTIONS (PlatformIO projects + Arduino IDE note).
- Masterplan status line.

- [ ] Implement.
- [ ] Run `py_compile`, `test_configurator.py` and a Guide switch test.
- [ ] Run `python desktop-app/build.py` (only if the app isn't running).
- [ ] Commit `feat(app): Sub Vendo flashing + docs + 2.0.0-dev`.

### Task 14: Full audit + hardware readiness

- [ ] Full test run:
  - `tools/test_zx_protocol.py`
  - `tools/vendo_test.py`
  - `tools/regression_test.py`
  - `desktop-app/test_configurator.py`
  - `node --check`
  - `pio run` for firmware, subvendo and selftest
- [ ] Code review pass over every changed file:
  - money paths: dedupe, persistence before ack, late/orphan rules;
  - auth on every admin endpoint;
  - signature checked before any state change;
  - integer and length validation;
  - heap-sized JSON docs;
  - pointer validity across vector changes;
  - mock/firmware parity (endpoint names, error codes, fields).
- [ ] Fix findings, adding a test for each where the mock can express it.
- [ ] Rebuild the bins and exes.
- [ ] Commit, and push branch `v2`.
- [ ] Hand the owner the hardware steps:
  - flash main v2 on COM5;
  - flash a second NodeMCU as the Sub Vendo;
  - Add Vendo → wizard → pair;
  - coin test;
  - `?vendo=` through the MikroTik captive redirect;
  - `freeHeap` with the sub polling.
