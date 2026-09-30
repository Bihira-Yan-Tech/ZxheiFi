# 🔌 Charging Station (v2)

A **Charging Station** is a coin-op phone charger with up to **4 USB ports**.
The customer presses a port's button on the box and drops coins; that port
gets power for the minutes the coins buy. They don't need a phone for any of
this, which matters because the phone is usually the thing that's dead.

**It works with no WiFi and with the main unit down.** The box runs the
charging itself. Every sale is stored in its flash and sent to the main unit
(signed, never counted twice) as soon as there is a connection.

## Buod (Tagalog)

1. Admin → Vendos → **+ Add Vendo** → Type **Charging Station** → kopyahin ang
   code.
2. Setup Companion → Flash Firmware → Device type **Charging Station**.
3. Sa phone: WiFi na **ZxheiFi-Charge-Setup** → ilagay ang WiFi ng lugar at ang
   code.
4. Admin → Settings → **Charging**: presyo (₱ → minuto), max bawat port, at wika
   ng screen.

**Paggamit:** pindutin ang button ng port, maghulog ng barya, at magcha-charge
na. Gumagana kahit walang WiFi. May **Stop** bawat port sa Admin, at makikita
ang kita sa Sales → **Charging**.

## Parts

| Part | Notes |
|---|---|
| NodeMCU (ESP8266) | the box's controller |
| Coin acceptor + its power relay | signal **D5**, relay **D7** (or "none"); 12 V supply for the acceptor |
| **PCF8574** I2C expander module (~₱60) | **D1 = SCL, D2 = SDA**; address **0x20** (A0-A2 to GND) |
| 4-channel relay module, **LOW-trigger** | inputs from PCF8574 **P0-P3** = ports 1-4 |
| 4 push buttons | between PCF8574 **P4-P7** and GND = ports 1-4 |
| 0.96" OLED **SSD1306** 128x64, I2C (~₱120) | same D1/D2 bus, address 0x3C; optional |
| Buzzer | **D8** |
| 5 V power supply, **10 A** | for 4 ports; a **polyfuse (~2.5 A) per port**; USB sockets wired to the relay outputs |

> Use **LOW-trigger** relay modules (the common kind). A PCF8574 pin can pull
> a relay input low, but it can't drive one high. If your module is
> HIGH-trigger, set **Port relays: HIGH** in Admin → Vendos → Edit.

## Wiring

```
NodeMCU            PCF8574 (0x20)          Relay module (4ch, LOW-trigger)
 3V3 ────────────── VCC                     VCC ── 5V
 GND ────────────── GND                     GND ── GND
 D1 (SCL) ───────── SCL      P0 ─────────── IN1   → USB port 1 (+5V)
 D2 (SDA) ───────── SDA      P1 ─────────── IN2   → USB port 2
                             P2 ─────────── IN3   → USB port 3
 OLED: VCC 3V3,              P3 ─────────── IN4   → USB port 4
 GND, SCL D1, SDA D2         P4..P7 ── button 1..4 ── GND
 D5 ── coin acceptor signal (common GND with the 12 V supply)
 D7 ── acceptor power relay
 D8 ── buzzer
```

## Setup

1. **Add the box.** Admin → Vendos → **+ Add Vendo**. Enter a name, choose
   **Type: Charging Station**, then Create. The pairing code is valid for
   15 minutes, one use. It counts toward the box limit (3 boxes on a NodeMCU
   main unit).
2. **Flash.** Setup Companion → Flash Firmware → Device type **Charging
   Station** → Flash.
3. **Wizard.** On a phone, join **ZxheiFi-Charge-Setup** and enter the WiFi of
   that spot, the main unit's address (default `nodemcu.zxheifi.lan`) and the
   code. The AP at that spot must be in **AP/bridge mode**, just as for a Sub
   Vendo.
4. **Prices.** Admin → Settings → **Charging**:
   - the price list (e.g. ₱5 = 30 min, ₱10 = 60 min, ₱20 = 150 min);
   - **max time per port** (default 180 min);
   - the **screen language** (Tagalog / English).

   Save Settings, and every Charging Station picks the changes up within a few
   seconds.
5. **Edit** (Vendos) sets these per box:
   - number of ports (1-4) and port relay HIGH/LOW;
   - coin pin, acceptor relay pin (D5/D6/D7 — D1/D2 are the I2C bus);
   - pesos per pulse and commission.

## How customers use it

| Step | Screen |
|---|---|
| Idle | `1: LIBRE   2: 0:23:10` ... `Pindutin ang port` |
| Press port 3 | `Port 3  18s` · `Maghulog ng barya` · `P10 = 60 min` · `Oras: 1:00:00` |
| 20 s without a coin | back to the list; port 3 keeps charging |
| Last minute | the port's time blinks |
| Time's up | the port turns off, beep |

**Rules**
- **Adding time:** press the port again while it runs.
- **Pricing:**
  - A coin gets its **exact price's** minutes.
  - A value with no exact price is worked out from the **cheapest** price
    (₱7 with ₱5 = 30 min → 42 min). No coin is wasted.
- **Max per port:** at the limit the acceptor stops taking coins for that
  port. A coin already falling in is still credited.
- **Acceptor relay set to "none"** (acceptor always on): a coin dropped with
  no port selected waits 60 s for the next button press. If nobody claims it,
  it's still counted in sales (`charge_unclaimed` in Logs).
- **Power cut:** each port's time is saved every 30 s and on every change,
  and resumes when the power is back. Any error is at most 30 s, in the
  customer's favour.

## In Admin

- **Vendos:**
  - the row shows `P1 libre · P2 23m · ...`, with **Stop** on a running port
    (staff can too);
  - a stop reaches the box within a few seconds, and the lost minutes are
    logged (`charge_stop`) so you can refund;
  - Collected, commission, Re-pair and Remove work as for a Sub Vendo.
- **Sales:**
  - a **Charging** column, included in totals and the CSV;
  - Sales → pick the box to see its commission split.
- **Telegram:** "Charging sale (Kanto): ₱10 = 60 min, port 2".

## Offline safety

| Situation | What happens |
|---|---|
| WiFi / main unit down | Charging goes on; sales wait in flash (up to 100, and more beyond that as one combined record); the screen shows "!" |
| Box removed in Admin | Running ports finish; no new coins; the screen asks for setup |
| Not paired / no prices yet | The acceptor stays off; the screen explains |

## Troubleshooting

| Problem | Fix |
|---|---|
| Blank screen | OLED wiring (3V3, GND, D1, D2). Charging still works |
| Triple-blink LED, ports never switch | PCF8574 not found: wiring, address 0x20 |
| Ports on while free | HIGH-trigger relay module → Edit → Port relays HIGH |
| Coins not accepted | Prices set? Box paired (no "Hindi pa naka-setup" on screen)? Acceptor relay pin/level right? |
| "!" on the screen | No link to the main unit: check the AP (bridge mode) and main address. Sales will sync later |

## For developers

- `charging/charge_logic.h`: every charging rule in pure C++, host-tested
  (`tools/run_host_tests.py` → `common/host_test/test_charge_logic.cpp`).
- `charging/charge_hw.h`: expander, buttons, screen.
- `charging/charging.ino`: wiring and the link, using `common/box/` shared
  with the Sub Vendo.
- **Main unit:** `firmware/vendo_api.h`.
  - `POST /api/vendo/charge {v,n,seq,peso,port,minutes}`
  - Polls carry `{ports, ackStop}`; replies carry `stop:[{id,port}]`.
  - `POST /api/admin/vendos/stop {id, port}`
- **Tests:** `tools/vendo_test.py` (`ChargeSim` in `tools/sub_sim.py`).
- **Spec:** `docs/superpowers/specs/2026-09-30-charging-station-design.md`.
