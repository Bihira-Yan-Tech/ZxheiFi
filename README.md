# ZxheiFi v2.0.0-dev

> Branch `v2` - in development. The stable release is
> [v1.0.0](https://github.com/Bihira-Yan-Tech/ZxheiFi/releases/tag/v1.0.0) on `main`.

Coin-op / voucher / PPPoE WiFi ("piso WiFi") system for **MikroTik + NodeMCU
(ESP8266)** — the "insert coin, get internet" hotspot you see at sari-sari
stores, laundry shops and small ISPs in the Philippines. Comes with a
Windows **Setup Companion** app that flashes the NodeMCU, configures the
MikroTik and uploads the customer portal for you.

- **MikroTik** runs the network: open customer WiFi, hotspot login, PPPoE,
  firewall, bandwidth limits.
- **NodeMCU** runs the business: coin slot, vouchers, sessions, sales,
  admin dashboard API — talking to the MikroTik over its RouterOS API.
- **Portal pages** (login, status, admin, voucher print sheet) are served by
  the MikroTik itself, so they load before the customer has internet.

## Features

**New in v2: Sub Vendos** - extra coin boxes (a NodeMCU + coin acceptor at
another spot) that sell time for the same WiFi. Pair them with a one-time code,
print a QR sticker per box, see status / income / coin-box contents and an
optional commission per box, and every coin travels over a signed, replay-proof,
fail-closed protocol. See [docs/14-sub-vendo.md](docs/14-sub-vendo.md).

**New in v2: Charging Station** - a coin-op phone charger box (4 USB ports,
a button per port, small OLED screen) that keeps working with no WiFi and
syncs every sale to the main unit. See [docs/15-charging-station.md](docs/15-charging-station.md).

**For customers**
- Open WiFi with a login page: **Insert Coin** (live ₱/minutes counter,
  auto-connect) or a **voucher** code (type it or scan its QR).
- Price list (amount / time / validity as `01d:00H:00M`) and an
  announcement banner on the login page.
- Status page with countdown, pause/resume, **Add Time with coins**,
  extend with a voucher, disconnect.
- Optional sounds (insert-coin prompt, coin drop, countdown music,
  success, error) — a starter set is included.

**For the owner (admin dashboard at `http://10.0.0.1/admin.html`)**
- **Rate Profiles** — one table for coins *and* vouchers:
  ₱ → minutes, data, validity, speed. Each coin is matched to its exact
  denomination.
- **Speed Profiles** — named bandwidth limits (e.g. "Piso 2M" = 2/1 Mbps),
  created on the MikroTik automatically.
- **Vouchers** — generate and print a sheet with QR codes (or Save as PDF).
- **Sales** — per-day coin/voucher/subscription totals, date-range filter,
  **Export CSV**; survives reboots.
- **Subscriptions** (monthly accounts), **multiple admins** (super/staff),
  **activity log**, **kick/block** devices, branding, Telegram sale alerts.
- **Coin Slot settings** — coin pin, relay pin, relay HIGH/LOW trigger,
  pesos per pulse; no reflash needed for a different baseboard.

**Setup Companion (Windows, portable or installer)**
- **Flash Firmware** — USB ports listed first, MAC read automatically,
  optional "erase everything" fresh start, serial monitor.
- **Configure MikroTik** — checks it's the right router (board, device-mode,
  direct connection) before changing anything; idempotent Config;
  **Upload GUI Files**; **Remove ZxheiFi Config**; **Network Check**.
- **Guide** — full walkthrough and troubleshooting, **English or Tagalog**.

## Repository layout

```
firmware/        Main unit firmware (NodeMCU ESP8266) + zxheifi_firmware.bin (ready to flash)
subvendo/        Sub Vendo firmware + zxheifi_subvendo.bin (ready to flash)
charging/        Charging Station firmware + zxheifi_charging.bin (ready to flash)
common/          zx_protocol.h (signed main <-> box protocol) + box/ (code shared by every box)
mikrotik/        Customer portal (gui/) + generated RouterOS scripts (*.rsc)
desktop-app/     Setup Companion (Python + CustomTkinter), build.py -> exe/installer
vouchers/        Offline voucher generator
tools/           mock_server.py (no-hardware test server), regression_test.py, vendo_test.py,
                 sub_sim.py (simulated sub vendo), run_host_tests.py (C++ tests on the PC)
docs/            Documentation, changelog, roadmap and the v2 masterplan
```

## Quick start

1. Get the **Setup Companion** (release folder `Portable/` or `Installer/`).
2. Open its **Guide** tab and follow it. In short:
   1. **Flash Firmware** to the NodeMCU (USB).
   2. Reset the hAP lite to *No Default Configuration*, give it a setup
      address, then **Configure MikroTik** → Test Connection → Config.
   3. **Upload GUI Files**.
   4. Run the NodeMCU **Setup Wizard** from a phone (`ZxheiFi-Setup` WiFi).
   5. Log in to the admin dashboard, add Speed + Rate Profiles, and test
      with a voucher and a coin.
3. Hardware and wiring: [docs/02](docs/02-hardware-setup.md),
   [docs/11](docs/11-coin-acceptor-wiring.md).

### Developing without hardware

```bash
python tools/mock_server.py        # portal + fake NodeMCU API on http://localhost:8080
python tools/regression_test.py    # API contract tests
python tools/vendo_test.py         # sub-vendo contract tests (pairing, signatures, coins, sales)
python tools/run_host_tests.py     # C++ protocol tests on the PC
python desktop-app/test_configurator.py   # MikroTik configuration tests (fake router)
python desktop-app/main.py         # run the Setup Companion from source
python desktop-app/build.py        # build the portable exe + installer
```

## Documentation

[`docs/`](docs/) — overview, hardware, MikroTik (hAP lite / hEX), flashing,
portal customization, features, FAQ/troubleshooting, coin acceptor wiring,
setup wizard, [changelog](docs/09-changelog.md), [roadmap](docs/10-feature-roadmap.md)
the [v2 masterplan](docs/13-v2-masterplan.md), [Sub Vendo](docs/14-sub-vendo.md) and
[Charging Station](docs/15-charging-station.md).

## Credits

- **ZxheiFi** — design and development.
- Inspired by **[JuanFi](https://github.com/ivanalayan15/JuanFi)** by
  **Ivan Julius Alayan**, the open-source coin-slot system for MikroTik
  hotspots that started it all for the Philippine piso-WiFi community.
  ZxheiFi is an independent rebuild, not affiliated with or endorsed by
  the JuanFi project.
- Open-source components are listed in
  [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## License

[MIT](LICENSE) © 2026 ZxheiFi. Free to use, modify and distribute, including
commercially — keep the copyright notice. The Windows executables also
contain third-party components under their own licenses (see
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)). Practical notes for running
a real coin-op business: [TERMS.md](TERMS.md) (not legal advice).
