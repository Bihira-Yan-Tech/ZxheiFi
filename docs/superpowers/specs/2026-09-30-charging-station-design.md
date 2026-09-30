# Charging Station - Design (v2, part 2 of 4)

- **Date:** 2026-09-30
- **Status:** approved in brainstorming (all four sections).
- **Branch:** `v2` (folder `ZxheiFi-v2`).
- **Builds on:** [Vendos framework + Sub Vendo](2026-09-26-vendos-framework-subvendo-design.md).

## Buod (Tagalog)

- **Ang box:** coin-op na charging box na may 4 na USB port, NodeMCU, coin acceptor, maliit na OLED screen, at isang button bawat port (sa pamamagitan ng PCF8574 I2C expander).
- **Ang customer:** pipindot ng port, maghuhulog ng barya, at magcha-charge. Hindi niya kailangan ng phone, dahil madalas lowbat ito.
- **Gumagana kahit walang WiFi o patay ang main unit.** Ang box ang nagpapatakbo ng charging, at bawat bayad ay itinatabi sa box at sini-sync sa main (may pirma, walang doble, walang nawawala).
- **Admin:**
  - "Charging Station" bilang uri ng vendo;
  - lagay ng bawat port at Stop;
  - Settings → Charging Rates (₱ → minuto), max bawat port, wika ng screen;
  - bagong column na "Charging" sa Sales.

## 1. Decisions made with the owner

| Question | Decision |
|---|---|
| Choosing a port / seeing time with a dead phone | Buttons + small screen on the box (OLED SSD1306 + PCF8574 expander) |
| Ports per box | 4 (Admin can set 1-4 per box) |
| Where | Its own box (own NodeMCU + coin acceptor), on the WiFi like a Sub Vendo |
| Architecture | **A - the box runs the charging**; sales are synced to the main unit. It works with no WiFi or with the main unit down |

## 2. Hardware (per box)

| Part | Pin / bus |
|---|---|
| Coin acceptor signal | D5 (configurable: D5 / D6 / D7) |
| Acceptor power relay | D7 (configurable: D5 / D6 / D7 / none) |
| Buzzer | D8 |
| Status LED | D4 (built-in) |
| Setup button | D3 (FLASH) |
| I2C bus | D1 = SCL, D2 = SDA |
| PCF8574 @ 0x20 | P0-P3 = port relays 1-4; P4-P7 = port buttons 1-4 (to GND, read LOW when pressed) |
| OLED SSD1306 128x64 @ 0x3C | same I2C bus |
| 4-channel relay module | switches 5 V to each USB port. Use **active-LOW** modules, the common kind; a PCF8574 can sink but not source relay current |
| 5 V supply | 10 A for 4 ports, plus a polyfuse (~2.5 A) per port |

- **Missing OLED:** everything still works; there is just no screen.
- **Missing expander:** the box reports "no expander" on its serial log and on the LED (triple blink), and keeps the acceptor off.

## 3. Customer flow (on the box)

1. **Idle.** The screen lists every port: `1: LIBRE  2: 00:23:10 ...` plus "Pindutin ang port".
2. **Press a port's button.** That port is selected for **20 s**. The acceptor relay turns on and the screen shows `Port 3 - Maghulog ng barya`, the ₱ inserted, the time added and the countdown.
3. **Each coin adds time.**
   - Exact Charging Rate for the value (₱5 = 30 min ...).
   - With no exact rate, `peso × minutes/peso` of the **lowest-peso rate** is used, so no coin is ever wasted.
   - The port relay turns on as soon as it has time. Every coin restarts the 20 s window.
4. **20 s without a coin** closes the window: the acceptor turns off and the screen goes back to the list.
5. **Adding time.** Press the button of a running port.
6. **Cap.** A port can't be topped up beyond **max per port** (default 180 min). At the cap the acceptor turns off. A coin already in flight is still credited, because money is never refused after the fact.
7. **Timeout.** When a port reaches 0 it turns off and the buzzer beeps. In the last minute its time blinks on the screen.
8. **Power cut.** Every port's remaining time is saved every 30 s and on every change, then resumed at boot. Time doesn't run while the power is off, and any error is at most 30 s in the customer's favour.
9. **Coin with no port selected.** This is only possible when the acceptor relay is "none". The coin is held for **60 s** for the next port pressed; after that it is reported as an unclaimed sale (see §5).

## 4. Software components

### 4.1 `common/box/` - shared by Sub Vendo and Charging Station

The code moves out of `subvendo/` and gets generalized. There is no behaviour change for the Sub Vendo.

| File | Contents |
|---|---|
| `box_config.h` | Board pins (LED D4, buzzer D8, setup D3), debounce/pulse constants, `boxPinGpio()` |
| `box_store.h` | `BoxState`, stored in LittleFS `/box.json`. Fields: WiFi, main host, pair code, vendo id, key, name, `cfgVer`, `counterBase`, `lastStopId`, plus the raw last config JSON (each firmware parses its own fields) |
| `record_queue.h` | Persistent queue of `{seq, peso, rid, port, minutes}` with a configurable capacity (Sub Vendo 20, Charging 100); `raiseSeq()` |
| `box_setup.h` | The captive Setup Wizard, parameterised by AP name and title |
| `box_link.h` | `postSigned()`, message counters (persisted in blocks), `pair()` |

### 4.2 `charging/` - Charging Station firmware

| File | Contents |
|---|---|
| `charge_logic.h` | **Pure C++, no Arduino**: ports, timers (ms), selection window, rates + fallback, cap, held credit, stop, snapshot/restore. Time is injected (`nowMs`), so it's host-tested |
| `charge_hw.h` | PCF8574 (relays + debounced buttons), OLED screens (TL / EN text), buzzer |
| `charging.ino` | Wiring. Link logic as in the Sub Vendo: pair / poll / config / send records |

### 4.3 Protocol additions (main unit)

**Registry.** `Vendo.type` is `"wifi"` (default, old files) or `"charging"`. Charging boxes also have `ports` (1-4) and `portActiveHigh` (default `false`). They count toward `MAX_SUB_VENDOS`.

**Pairing reply `config`.** Adds `type`. For a charging box it also carries `ports`, `portActiveHigh`, `rates:[{peso,minutes}]`, `maxMinutes` and `lang` ("tl" | "en").

**Poll.**
- A charging box sends `{v, n, ports:[remainingSec...], ackStop}`.
- The reply is `{n, cfgVer, name, stop:[{id,port}]}`.
- `cfgVer` reported to a charging box = `v.cfgVer + chargeCfgVer`, where `chargeCfgVer` is bumped whenever Settings → Charging changes. Any change makes the box refetch its config.

**`POST /api/vendo/charge {v, n, seq, peso, port, minutes}` → `{n, ok[, dup]}`.**
- Only from charging boxes.
- `seq` shares the box's coin sequence (deduped by `lastCoinSeq`).
- Effects:
  - `boxTotal += peso`;
  - `AdminAPI::addChargeRevenue(id, peso)` updates `chargingRevenueToday` and `byVendo`;
  - log `charge` ("PHP 10 = 60 min, port 2"), or `charge_unclaimed` for `port 0`;
  - a Telegram "Charging sale (Kanto): ₱10 = 60 min, Port 2".
- `port 0, minutes 0` = an unclaimed or aggregated-overflow sale.

**Stop.**
- `POST /api/admin/vendos/stop {id, port}` (staff+) queues `{id: ++stopSeq, port}` for that box and logs `charge_stop`.
- It is delivered in poll replies until the box acknowledges it (`ackStop >= id`).
- The box persists `lastStopId`, so a stop is never applied twice.

**Type rules.**
- `/api/vendo/coin` from a charging box → 400 `wrong_type`.
- `/api/vendo/charge` from a WiFi box → 400 `wrong_type`.
- `coin/start` on a charging vendo → 404 `vendo_unknown`.
- Branding `vendos` lists WiFi boxes only.

### 4.4 Settings

Stored on the main unit in `AdminAPI` / `config.json`:

| Setting | Rule |
|---|---|
| `chargeRates` | up to 10 of `{peso 1-1000 (unique), minutes 1-1440}` |
| `chargeMaxMinutes` | 10-1440, default 180 |
| `chargeLang` | "tl" / "en", default "tl" |

Defaults: `[{5,30},{10,60},{20,150}]`.

### 4.5 Sales

- `DailySalesEntry` and today's file gain `chargingRevenue`.
- `byVendo` counts every peso a box took (WiFi coins or charging).
- The day total, Overview "revenue today", the Sales table (new **Charging** column), the CSV and the per-vendo views all include it.

### 4.6 Admin UI

- **Vendos → Add Vendo** becomes an inline form: name + type (WiFi coin box / Charging Station) + Create. It shows the pairing code as before.
- **Charging rows** show `P1 libre · P2 23m · ...` with **Stop** buttons (staff+) and no QR. Edit adds ports 1-4 and port relays HIGH/LOW. The coin pin choices exclude D1/D2 (the I2C bus).
- **Settings → Charging:** a rates table, max per port and screen language.

### 4.7 Setup Companion

Device type gets a third option, **Charging Station** (`charging/zxheifi_charging.bin`, bundled). The Guide gains an EN/TL "Charging Station" section, and a new doc `docs/15-charging-station.md` is added.

## 5. Error handling

| Situation | Behaviour |
|---|---|
| No WiFi / main unit down | Charging continues. Sale records queue (up to 100); beyond that, extra pesos go into one persisted "overflow" total, which is queued as a single record (`port 0`) once there is room - no service stop, no lost sales |
| Box removed/re-paired (401) | Running ports finish; the acceptor stays off; the screen says it needs setup |
| Not paired / no rates yet | The acceptor stays off; the screen shows the setup instructions |
| Admin Stop | The port turns off immediately; the forfeited minutes are logged on the main unit |
| Power cut | Ports resume from the last save (≤ 30 s customer-favourable error) |
| Held coin not claimed in 60 s | Queued as `port 0` (unclaimed sale), logged `charge_unclaimed` |
| Duplicate / replayed / forged messages | As in part 1 (seq dedupe, counters, HMAC) |

## 6. Testing

1. **Host C++** (`tools/run_host_tests.py`), `test_charge_logic.cpp`:
   - rates, fallback, cap;
   - top-up of a running port;
   - countdown and timeout;
   - selection window;
   - held credit, claim and expiry;
   - stop;
   - snapshot/restore.
2. **Mock + `tools/sub_sim.py`** (`ChargeSim`) in `tools/vendo_test.py`:
   - pairing a charging box (config with rates);
   - a charge sale → sales `chargingRevenue` + `byVendo` + box;
   - dedupe;
   - `wrong_type` both ways;
   - Stop delivery + ack;
   - settings validation + `cfgVer` bump;
   - branding excludes charging boxes;
   - `coin/start` refused on a charging vendo;
   - limit counts both types.
3. `regression_test.py` stays 103/103. The part-1 vendo tests stay green.
4. Firmware: `pio run` for main, subvendo, charging, selftest - no new warnings.
5. Browser (mock):
   - Add Vendo form with type;
   - charging row + Stop;
   - Settings → Charging;
   - Sales Charging column + CSV.
6. **Hardware (owner, later):** expander + OLED wiring, relays, button feel, power-cut resume.

## 7. Out of scope

Current sensing / auto-stop when full, GCash, choosing a port from a phone, ESP32 (part 4).
