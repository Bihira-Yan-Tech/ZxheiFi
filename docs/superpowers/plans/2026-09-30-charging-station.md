# Charging Station Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A coin-op 4-port phone-charging box (NodeMCU + PCF8574 + OLED) that runs on its own, syncs every sale to the main unit over the part-1 signed protocol, and is managed from the Vendos tab.

**Architecture:**
- The box owns charging (pure-C++ `ChargeLogic`, host-tested) and queues sale records.
- The main unit records sales, pushes rates/config via `cfgVer`, and relays admin Stop commands through poll replies.
- The shared box plumbing (store, queue, wizard, signed link) moves from `subvendo/` to `common/box/`, so both boxes use one copy.

**Tech Stack:** ESP8266 Arduino + ArduinoJson 6 + ThingPulse SSD1306 driver (PlatformIO), Wire (PCF8574), Python mock/tests, vanilla JS portal, CustomTkinter app.

**Spec:** [docs/superpowers/specs/2026-09-30-charging-station-design.md](../specs/2026-09-30-charging-station-design.md)

## Global Constraints

**Types and fields**
- Vendo types: `"wifi"` (the default, and what older files read as) and `"charging"`. Both count toward `MAX_SUB_VENDOS`.
- Charging box `ports` is 1-4 (default 4). `portActiveHigh` defaults to `false`.

**Charging box pins**
- Coin pin: D5 / D6 / D7.
- Acceptor relay: D5 / D6 / D7 / none, and never the same pin as the coin.
- I2C: SDA = D2, SCL = D1. PCF8574 at 0x20; OLED at 0x3C.

**Box timings** (`charge_logic.h`)

| Constant | Value |
|---|---|
| Selection window | 20 000 ms |
| Held coin | 60 000 ms |
| Port save interval | 30 000 ms |
| Blink warning | last 60 s |

**Settings** (on the main unit)
- `chargeRates`: at most 10; peso 1-1000 and unique; minutes 1-1440.
- `chargeMaxMinutes`: 10-1440, default 180.
- `chargeLang`: "tl" or "en".
- Default rates: `[{5,30},{10,60},{20,150}]`.

**Rate rule**
- An exact peso match uses its rate.
- Otherwise: `minutes = peso * lowestPesoRate.minutes / lowestPesoRate.peso` (integer).
- With no rates at all, the minutes are 0 and the acceptor stays off.

**Protocol and queues**
- `POST /api/vendo/charge {v,n,seq,peso,port,minutes}` → `{n,ok[,dup]}`. `port 0` = unclaimed or overflow sale.
- Poll: a charging box sends `{v,n,ports:[sec...],ackStop}`; the reply adds `stop:[{id,port}]`. `cfgVer` = `v.cfgVer + chargeCfgVer`.
- Charging record queue: 100 records. Sub Vendo coin queue: 20.

**Never break**
- `regression_test.py` stays 103/103.
- The part-1 `vendo_test.py` checks stay green.
- The host C++ tests pass.
- No new compiler warnings.

## File map

| File | Change |
|---|---|
| `common/box/box_config.h`, `box_store.h`, `record_queue.h`, `box_setup.h`, `box_link.h` | New shared box code (moved from `subvendo/`, generalized) |
| `subvendo/*` | Uses `common/box/`; `sub_store.h`, `coin_queue.h`, `sub_setup.h` deleted; `sub_config.h` slimmed |
| `charging/charge_logic.h` | New, pure C++ |
| `charging/charge_hw.h`, `charging/charging.ino`, `charging/platformio.ini`, `charging/zxheifi_charging.bin` | New |
| `common/host_test/test_charge_logic.cpp` | New |
| `firmware/admin_api.h` | Charge settings, `chargingRevenue`, `addChargeRevenue` |
| `firmware/vendo_registry.h` | `type`, `ports`, `portActiveHigh`, `portSecs`, stop queue |
| `firmware/vendo_api.h` | `/api/vendo/charge`, poll ports/stops, type rules, `/api/admin/vendos/stop`, add/update types |
| `firmware/gui_handler.h` | Settings get/save charging, branding WiFi-only, sales `chargingRevenue`, overview |
| `firmware/coin_slot.h` | Refuse charging vendos |
| `tools/mock_server.py`, `tools/sub_sim.py` (`ChargeSim`), `tools/vendo_test.py` | Mirror + tests |
| `mikrotik/gui/*` | Add Vendo form + type, charging rows + Stop, Settings → Charging, Sales Charging column |
| `desktop-app/*`, `tools/make_release.py`, `docs/*` | Third device type, Guide, docs |

---

### Task 1: `ChargeLogic` (pure C++) + host tests

**Files:**
- Create: `charging/charge_logic.h`
- Create: `common/host_test/test_charge_logic.cpp`

**Interfaces - Produces** (namespace `zxc`)

Structs:
- `struct ChargeRate { uint32_t peso, minutes; }`
- `struct ChargeSale { uint8_t port; uint32_t peso, minutes; }`

`class ChargeLogic`, configuration and state:
- `void configure(uint8_t ports, const ChargeRate* rates, uint8_t nRates, uint32_t maxMinutes)`
- `void setEnabled(bool)`
- `uint8_t ports() const`
- `uint32_t remainingMs(uint8_t port) const` (ports are 1-based)
- `bool portOn(uint8_t) const`
- `uint8_t selected() const` (0 = none)
- `bool acceptorOn() const`
- `bool atCap(uint8_t) const`
- `uint32_t minutesFor(uint32_t peso) const`
- `uint32_t heldPeso() const`

Inputs:
- `void press(uint8_t port, uint32_t nowMs, ChargeSale* claimed, bool* hasClaim)` - a held coin is credited to the pressed port, and the resulting sale is reported through `claimed`.
- `bool coin(uint32_t peso, uint32_t nowMs, ChargeSale& sale)` - returns `true` when credited to a port (fills `sale`), `false` when held.
- `void tick(uint32_t nowMs, ChargeSale* expired, bool* hasExpired)` - counts time down, closes the window, and expires a held coin as a `port 0` sale.
- `uint32_t stop(uint8_t port)` - returns the forfeited ms.

Persistence:
- `void snapshot(uint32_t outSec[4]) const`
- `void restore(const uint32_t inSec[4], uint32_t nowMs)`

**Tests:**
- exact rate;
- fallback (₱7 with the lowest rate ₱5 = 30 → 42 min);
- no rates → 0 and the acceptor off;
- press then coin → port on with the right ms;
- countdown (`tick` +61 s) and timeout → off;
- the window closes 20 s after the last activity, and a coin resets it;
- top-up of a running port;
- cap: acceptor off when `remaining >= max`, but an in-flight coin is still credited;
- a coin with no selection is held, then claimed by `press`;
- a held coin expires after 60 s as a `port 0` sale;
- `stop` returns the forfeited ms and turns the port off;
- snapshot/restore round-trip;
- disabled → acceptor off, running ports keep counting;
- `ports=2` → pressing port 3 is ignored;
- `millis` wrap-around is safe.

- [ ] Write the tests, then run `python tools/run_host_tests.py`. Expected: FAIL (no header).
- [ ] Implement `charge_logic.h`.
- [ ] Run the tests. Expected: PASS.
- [ ] Commit.

### Task 2: Shared box plumbing `common/box/` + Sub Vendo on it

**Files:**
- Create: `common/box/{box_config.h, box_store.h, record_queue.h, box_setup.h, box_link.h}`
- Modify: `subvendo/subvendo.ino`, `subvendo/sub_config.h`
- Delete: `subvendo/{sub_store.h, coin_queue.h, sub_setup.h}`

**Interfaces - Produces:**

`BoxState` - fields `wifiSsid`, `wifiPass`, `mainHost`, `pairCode`, `vendoId`, `key[32]`, `name`, `cfgVer`, `counterBase`, `lastStopId`, `String configJson`:
- `load()`, `save()`
- `configured()`, `paired()`
- `applyConfig(JsonVariantConst)` - stores name + `cfgVer` + the raw JSON.

`RecordQueue` - records `{seq, peso, rid, port, minutes}`:
- constructed with `(const char* file, uint8_t capacity)`
- `load()`, `push(peso, rid, port, minutes)`, `pop()`, `front()`
- `size()`, `empty()`, `full()`, `raiseSeq(n)`

`BoxSetup` - constructed with `(const char* apSsid, const char* title)`:
- `static buttonPressedInWindow()`
- `begin(server, state)`, `loop()`

`BoxLink` - constructed with `(BoxState&)`:
- `begin()` (counter block)
- `nextCounter()`
- `postSigned(path, payload, key, resp) -> int`
- `pair(String firmwareHint, JsonDocument& resp) -> bool`

`box_config.h`: `boxPinGpio()` plus the LED/buzzer/button pins and pulse constants.

- [ ] Implement.
- [ ] Port `subvendo.ino`.
- [ ] Run `pio run -d subvendo` (no warnings) and the host tests.
- [ ] Commit.

### Task 3: Mock - charging type, settings, charge endpoint, stops, sales; `ChargeSim`; tests

**Files:**
- Modify: `tools/mock_server.py`, `tools/sub_sim.py`, `tools/vendo_test.py`

**Behaviour** (mirrors spec §4.3-4.5):
- `add {name, type}` - `bad_type` on an invalid type;
- `update`: ports, `portActiveHigh`, and charging pin rules;
- list: `type`, `ports`, `portSecs`, `portSecsAgoSec`;
- `POST /api/admin/vendos/stop {id, port}` (staff+) - `not_charging` / `bad_port`;
- `/api/vendo/charge`;
- poll ports / `ackStop` / `stop`;
- `wrong_type` both ways; `coin/start` on a charging vendo → 404;
- branding lists WiFi boxes only;
- settings `chargeRates`, `chargeMaxMinutes`, `chargeLang` (validated), with `chargeCfgVer` bumped on change;
- sales `today.chargingRevenue`; `byVendo` includes charging.

`ChargeSim(SubSim)`:
- `charge(peso, port, minutes)`
- `poll(ports=[...])` - applies stops and acks them.

**Tests:**
- pair a charging box, and its config carries rates, `maxMinutes`, `lang`, `ports=4`, `type`;
- a charge sale → `chargingRevenue` 10, `byVendo {1: 10}`, box 10, log `charge`;
- a resent seq → dup, counted once;
- a coin from a charging box → 400 `wrong_type`; a charge from a WiFi box → 400;
- `coin/start` vendo 1 (charging) → 404;
- branding excludes the charging box;
- admin Stop → the next poll carries the stop; ack → gone; staff allowed; `bad_port`;
- a settings charging update bumps the box's `cfgVer`, and validation errors (`bad_charge_rates`, `bad_charge_max`, `bad_charge_lang`);
- the list shows `portSecs` from the poll;
- charging + WiFi boxes share the limit of 3;
- a `port 0` sale → log `charge_unclaimed`.

- [ ] Write the tests.
- [ ] Run them: they fail.
- [ ] Implement.
- [ ] Run all suites: they pass.
- [ ] Commit.

### Task 4: Firmware main unit - charging support

**Files:**
- Modify: `firmware/admin_api.h`, `firmware/vendo_registry.h`, `firmware/vendo_api.h`, `firmware/gui_handler.h`, `firmware/coin_slot.h`

Implements the same contract as the Task 3 mock.
- [ ] Run `pio run -d firmware`. Expected: no warnings.
- [ ] Copy the bin to `firmware/zxheifi_firmware.bin`.
- [ ] Commit.

### Task 5: Charging Station firmware

**Files:**
- Create: `charging/charge_hw.h`, `charging/charging.ino`, `charging/platformio.ini`, `charging/zxheifi_charging.bin`

**Behaviour:** spec §3 and §5.
- `lib_deps` add `thingpulse/ESP8266 and ESP32 OLED driver for SSD1306 displays@^4.4.0`.
- Records go to a `RecordQueue` of 100.
- Overflow: pesos go into `state.overflowPeso`, persisted, and are pushed as a `port 0` record when there is room.
- Ports are saved to `/ports.json` every 30 s and on every change.

- [ ] Run `pio run -d charging`.
- [ ] Copy the bin.
- [ ] Commit.

### Task 6: Portal

**Files:**
- Modify: `mikrotik/gui/{admin.html, script.js, style.css}`

**Changes:**
- Add Vendo inline form (name + type).
- Charging rows: port chips + Stop.
- Edit gets ports + port relay level; coin pin options are D5-D7 for charging.
- Settings → Charging section: rates rows + max + language; saved with the rest of Settings.
- Sales: Charging column, day totals, CSV column.

- [ ] Run `node --check` and a browser check on `zxheifi-v2-mock` with a `ChargeSim` feeding sales and ports.
- [ ] Commit.

### Task 7: App, Guide, docs, release

**Files:**
- Modify: `desktop-app/main.py` (the third device type and `DEFAULT_CHARGING_FIRMWARE`)
- Modify: `desktop-app/build.py` (bundle it)
- Modify: `desktop-app/guide_content.py` (an EN/TL "Charging Station" section)
- Modify: `tools/make_release.py` (ship the bin)
- Create: `docs/15-charging-station.md`
- Modify: changelog, README, `firmware/BUILD_INSTRUCTIONS.md`, masterplan status

- [ ] Run `py_compile`, the Flash tab device-type test and `test_configurator`.
- [ ] Commit.

### Task 8: Full audit + builds

- [ ] Run every suite, and `pio run` for firmware, subvendo, charging and selftest.
- [ ] Review pass (money paths, auth, validation, heap, mock/firmware parity) and fix the findings.
- [ ] Rebuild the exes (clean) and `make_release.py`.
- [ ] Update progress.md and memory.
- [ ] Push branch `v2`.
