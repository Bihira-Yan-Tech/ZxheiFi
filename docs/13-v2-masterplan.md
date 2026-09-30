# 🚀 ZxheiFi v2 Masterplan

Drafted 2026-09-26, right after v1.0.0. It's a brainstorm and a plan to
choose from, not a commitment. Each item gets scoped with the owner
before it's built.

> **Status (2026-09-26):** Part 1 - Vendos framework + **Sub Vendo** - built on
> branch `v2` (see [14-sub-vendo.md](14-sub-vendo.md)); awaiting hardware test.
> Part 2 - **Charging Station** - built 2026-09-30 (see [15-charging-station.md](15-charging-station.md));
> awaiting hardware test. Next: ESP32 main unit + backup/restore.

## Buod (Tagalog)

Ang v1.0.0 ay kumpleto na bilang isang **single-vendo** piso WiFi: may
coin, voucher, PPPoE, admin panel, sales, print, sounds at setup app. Sa
v2, ang layunin ay maging **isang buong "vendo platform"**:

1. **Sub Vendo** — dagdag na coin slot sa ibang lugar (ibang kwarto o
   kanto), iisang sales at admin.
2. **Charging Station** — coin-op na charger (per port, may timer), kasama
   sa parehong sales.
3. **E-wallet (GCash/Maya QR Ph)** — magbabayad kahit walang barya.
4. **Remote Dashboard** — makikita ang kita at makokontrol ang vendo kahit
   wala ka sa shop.
5. **Fair Time** — kapag nawala ang internet ng ISP, **hihinto ang oras**
   ng lahat ng customer. Bihira ito sa ibang vendo at malaking tiwala ng
   customer.

Ang mga dahilan kung bakit mas lamang ito sa JuanFi at sa iba pa ay nasa
talahanayan sa ibaba.

## Where v1.0.0 stands

Already ahead of most piso WiFi systems:
- A Windows setup app that flashes, configures and uploads, and guards
  against configuring the wrong router.
- Rate Profiles per coin value, named Speed Profiles, and PPPoE
  subscriptions.
- Multi-admin roles, a voucher print sheet with QR codes, sales with CSV
  export, and configurable coin pins.
- Open WiFi and a bilingual guide.

Known limits that v2 should address:
- One NodeMCU per unit.
- ESP8266 memory (~27 KB free heap). This caps table sizes and rules out
  TLS-heavy features.
- The admin panel works on the LAN only.
- Coins are the only way to pay.

## Competitive picture

| Capability | JuanFi | Typical commercial vendo (Orange Pi / "Piso WiFi" boards) | ZxheiFi v1 | ZxheiFi v2 target |
|---|---|---|---|---|
| Setup without terminal commands | ⚠️ web pages + manual RouterOS | ✅ closed image | ✅ Setup Companion | ✅ + one-click updates |
| Multi-vendo / sub vendo | ✅ | ✅ | ❌ | ✅ ESP-NOW + WiFi |
| Charging station | ➖ separate project | ✅ some | ❌ | ✅ integrated sales |
| E-wallet (GCash/Maya) | ❌ | ⚠️ few, often manual | ❌ | ✅ QR Ph |
| Remote monitoring | ✅ Android app + cloud | ✅ | ❌ | ✅ Telegram bot + optional cloud |
| Time freeze on ISP outage | ❌ | ⚠️ rare | ❌ | ✅ |
| Open source, no license fee | ✅ | ❌ paid license per unit | ✅ MIT | ✅ MIT |
| Ethernet (wired) controller option | ✅ W5500 | ✅ | ❌ | ✅ |
| ESP32 | ✅ | n/a | ❌ | ✅ |

## Theme 1 — Multi-vendo (top priority)

### 1.1 Sub Vendo
A second (third, ...) coin-slot box somewhere else in the shop or
neighbourhood that sells time for the same hotspot.
- **Hardware:** a NodeMCU/ESP32 plus coin acceptor plus an optional
  small OLED. It needs no MikroTik of its own.
- **Link:** WiFi to the main hotspot (simplest). **ESP-NOW** is the
  option for spots where the WiFi is weak or busy; it's
  router-independent and has about 100 m line of sight.
- **Flow:** on the portal, the customer picks "Insert coin at: Main /
  Sub 1 (Tindahan) / ...".
  1. The main unit reserves that sub vendo.
  2. The sub vendo reports pulses to the main unit
     (`POST /api/subvendo/coin`, signed with a per-vendo key).
  3. The main unit credits them as it does today.
- **Admin:**
  - A Vendos tab: name, location, online/offline, last coin, coin box
    total since last collection.
  - Sales split per vendo.
  - An optional **commission %** per vendo for the host of a sub vendo
    (e.g. the store owner gets 20%). This is a real selling point for
    placing units in other people's stores.
- **Setup Companion:** a "Flash as Sub Vendo" mode that pairs to the
  main unit with a code shown in Admin.

### 1.2 Charging Station
A coin-op phone charger: 4–8 USB ports, each switched by a relay or
MOSFET.
- The customer drops a coin, picks a port on buttons/OLED (or on the
  portal's "Charge" tab), and gets N minutes of power on that port.
- **Charging Rate Profiles**, separate from WiFi rates
  (₱5 = 30 min, and so on).
- It can run **standalone** (no internet needed) and sync its sales to
  the main unit when reachable, so one sales report covers WiFi +
  charging.
- **Safety:**
  - Fused, per-port current limit.
  - Auto-off at the end of the timer.
  - "Port in use" protection.
  - A log entry for each session.

### 1.3 Other vendo types (same framework)
The Vendos framework is generic: "a box that takes coins and does
something for N minutes". That covers:
- **Water vendo** — a pump relay by volume or time.
- **Arcade / massage chair / aircon time.**
- **Printing credit.**

Each vendo type is a small firmware profile: which relays, which rate
table, what the display shows.

## Theme 2 — Payments beyond coins

### 2.1 GCash / Maya via QR Ph
- The portal shows a dynamic QR Ph for the chosen rate.
- Payment confirmation comes through a payment gateway (PayMongo,
  Xendit, or similar). A webhook goes to a small relay service, because
  the NodeMCU can't receive internet webhooks directly.
- On confirmation, the session is provisioned.
- This needs a merchant account (a business/procurement decision). The
  firmware side is small; the relay service is the real work.
- **Fallback for no merchant account:** "manual e-wallet". The customer
  pays to the owner's GCash and the owner confirms in the admin panel,
  which generates the time. A single tap. Many small shops already do
  this informally.

### 2.2 Bill acceptor
₱20/₱50/₱100 bills via a pulse-output bill acceptor. It uses the same
pulse logic as coins, needs its own pin, and uses its own
pesos-per-pulse setting.

### 2.3 Loyalty and promos
- **Points per peso**, redeemable for free minutes. Tied to the device,
  or to a phone number for customers who opt in.
- **Happy hour pricing:** time-based Rate Profiles, generalising Night
  Promo.
- **Bonus rules:** "₱20 gets +10 min", or "first coin of the day +5 min".
- **Referral voucher:** share a code, and both people get minutes.

## Theme 3 — Trust and reliability (what customers notice)

### 3.1 Fair Time (freeze on ISP outage)
The NodeMCU (or a MikroTik netwatch) pings two or three internet hosts.
When all of them fail:
- every active session is paused automatically;
- the login page says "Internet is down — your time is paused";
- sessions resume by themselves when the internet comes back;
- the outage is logged with its duration.

This is the single most visible fairness feature, and a strong reason
for customers to choose a ZxheiFi shop.

### 3.2 Dual-WAN failover / load balancing
Two ISPs (e.g. fibre + LTE modem) on the MikroTik. The Setup Companion
generates failover (or PCC load-balance) rules. The admin panel shows
which WAN is active.

### 3.3 Coin box and anti-theft
- **Coin-box counter:** pesos since last collection, plus a "Collected"
  button that logs who collected and when (cash reconciliation).
- **Tamper sensors:** a door switch or tilt sensor. The alarm buzzer
  sounds and a Telegram alert is sent.
- **Coin-acceptor health:** alerts on repeated `coin_anomaly` entries
  (a jammed slot, or a fishing attempt).

### 3.4 Wired controller option
An ESP32 + W5500 Ethernet (or the ESP32's own Ethernet PHY) for the
main controller. A payment kiosk shouldn't depend on WiFi.

## Theme 4 — Remote management

### 4.1 Telegram bot (two-way)
v1 only sends alerts. v2 accepts commands from the owner's chat ID:
- `/sales today`
- `/active`
- `/voucher 20 x5` (sends codes, or a PDF)
- `/kick`
- `/restart`
- `/announce ...`

It needs no cloud of our own and works through the NodeMCU's outbound
HTTPS. On the ESP8266 it must be memory-careful; it's comfortable on
the ESP32.

### 4.2 Optional ZxheiFi Cloud
A small self-hostable relay (Docker, or a free-tier VPS). Units connect
out to it; nothing is port-forwarded. It provides:
- a multi-unit dashboard, and sales across all shops;
- remote settings;
- the GCash webhook endpoint from 2.1.

It stays optional: every unit keeps working fully offline.

### 4.3 OTA updates
- Firmware update from the admin panel or from the Setup Companion over
  WiFi, with no USB needed.
- A signed image and a rollback partition on the ESP32.
- The Setup Companion checks GitHub Releases for new versions of the
  app, firmware and GUI together, and keeps the three versions in step
  (v1.0.0 already detects mismatches).

### 4.4 Backup and restore
Export all settings, rate/speed profiles, vouchers and sales to one file
from the admin panel or the Setup Companion. Restore it onto a
replacement NodeMCU in one step.

## Theme 5 — Customer experience

- **Portal languages:** English / Tagalog / Bisaya toggle on the login
  and status pages.
- **"Time almost up" warning:** a banner and sound on the status page at
  5 min and 1 min, with an "Add Time" shortcut.
- **Remember me:** returning devices see their paused time straight
  away.
- **Speed test button** on the status page, served by the MikroTik
  (bandwidth test to the router).
- **Sponsored banner slot:** a local business ad on the portal, as extra
  income for the owner. It stays text/image only, with no tracking
  scripts.
- **Mini-games while waiting** (offline, tiny). Optional; it's a common
  crowd-pleaser in PH vendos.

## Theme 6 — Owner tools

- **Voucher reseller mode:** staff or sari-sari resellers get a staff
  account that can only generate vouchers, with a per-reseller sales
  report and commission.
- **Thermal printer:** print vouchers straight to a 58 mm Bluetooth/USB
  printer from the admin panel, besides the current A4 sheet.
- **Charts:** 7/30-day income chart, peak hours, and top rate.
- **Scheduled announcements and scheduled restarts.**
- **Setup Companion "Health" tab:** one button that checks the router,
  NodeMCU, versions, free storage, and the time since the last coin.

## Platform decision for v2: ESP32 first, ESP8266 kept

The ESP8266's ~27 KB heap already limits settings size, and it rules out
comfortable TLS (Telegram bot, cloud, GCash). The proposal:
- **Main controller → ESP32** (e.g. ESP32-DevKitC; optional WT32-ETH01
  for wired Ethernet). One codebase with `#ifdef` for both chips.
- **Sub vendo / charging station → ESP8266 is fine.** They only count
  pulses, switch relays and talk to the main unit.
- Existing v1 units keep working. The v1 → v2 migration is: flash the
  new firmware, restore the backup.

## Proposed phasing

| Phase | Content | Why this order |
|---|---|---|
| **2.0** | Vendos framework + **Sub Vendo** + **Charging Station** + coin-box counter + backup/restore + ESP32 build | The owner's stated next step; the framework unlocks every other vendo type |
| **2.1** | **Fair Time** + "time almost up" warning + portal languages + dual-WAN failover | Customer trust; mostly MikroTik/firmware logic, no external accounts |
| **2.2** | **Telegram bot commands** + OTA updates + Setup Companion update checker/Health tab | Remote control without building a cloud |
| **2.3** | **GCash/Maya** (manual confirm first, then QR Ph gateway) + bill acceptor + loyalty/promos | Needs merchant-account decisions; manual mode ships first |
| **2.4** | Optional **ZxheiFi Cloud** + reseller mode + thermal printer + charts | Largest effort; only worth it with several units in the field |

## Open questions for the owner

1. **Sub vendo:** how far from the main unit? This decides WiFi vs
   ESP-NOW vs cable.
2. **Charging station:**
   - How many ports?
   - Standalone boxes, or built into the WiFi vendo cabinet?
   - Is a display wanted?
3. Is there a GCash/Maya **business** account (needed for automatic QR
   Ph)?
4. Go ESP32 for new main units, or stay on NodeMCU for cost?
5. Which three features would customers in your area notice most?

## Release engineering for v2

- Semantic versions: firmware, GUI and app share `MAJOR.MINOR`; the
  patch number may differ.
- GitHub Actions:
  - build the firmware with PlatformIO;
  - build the portable exe and the installer on a Windows runner;
  - run `test_configurator.py` and `regression_test.py`;
  - attach everything to a GitHub Release.
- A `CONTRIBUTING.md`, issue templates (bug report asks for the
  Network Check output + Serial Monitor log), and a Tagalog +
  English README.
