# 💾 ESP32 Main Unit + Backup/Restore (v2)

## Buod (Tagalog)

- **Backup:** Admin → Settings → **Backup & Restore** → *Download backup*. Isang
  file ito na laman ang lahat (settings, presyo, vouchers, subscribers, admins,
  sales, logs, aktibong customer, at mga box kasama ang keys). **Itago ito.**
  Wala rito ang WiFi at MikroTik password.
- **Restore:** piliin ang file, kumpirmahin, at magre-restart ang unit. Kapag
  sira o hindi ZxheiFi ang file, **walang mababago**.
- **ESP32 DevKit bilang main unit:** mas malakas na kapalit ng NodeMCU. Kaya
  nito hanggang **10 box**, kontra 3. Ang paglipat ay: Backup → flash ang ESP32
  → Setup Wizard → Configure MikroTik (bagong MAC) → Restore.

## Backup & Restore

**What a backup contains:**
- settings (incl. Charging and Coin Slot);
- rate + speed profiles;
- vouchers and subscribers;
- admin accounts (salted password hashes);
- sales history and today's totals;
- the activity log;
- live customer sessions (nobody loses paid time);
- coin boxes with their keys (no re-pairing needed);
- coin-box collections.

**What it does NOT contain:** `network.json`, i.e. the WiFi and MikroTik API
credentials. The board you restore onto gets those from its own Setup Wizard.

> ⚠️ Keep backups private. A backup holds your coin boxes' keys, so whoever has
> it could impersonate the main unit to your boxes.

**Restoring:**
1. Admin → Settings → Backup & Restore → choose the file → **Restore**.
2. The page shows which board, firmware and date the backup came from, and
   asks you to confirm.
3. The unit checks the **whole** file before touching anything. A damaged,
   truncated or foreign file is refused, and nothing changes.
4. The files are swapped in and the unit restarts. "restore" appears in Logs.
5. Coin/relay pins from another board (a NodeMCU's `D5` on an ESP32) are reset
   to the board's defaults and logged as `pins_reset`. Check Settings →
   Coin Slot afterwards.

Backups up to 1 MB are accepted; a busy shop's backup is typically 50–300 KB.

## ESP32 DevKit as the main unit

| | NodeMCU (ESP8266) | ESP32 DevKit (ESP32-WROOM-32) |
|---|---|---|
| Coin boxes (sub vendo + charging) | up to 3 | up to 10 |
| Coin pin (default) | D5 | **G14** |
| Relay pin (default) | D7 | **G13** |
| Buzzer | D8 | **GPIO25** |
| Status LED | built-in, on = LOW | built-in GPIO2, on = HIGH |
| Setup button | FLASH | **BOOT** |
| Other coin/relay choices | D1 D2 D6 | G26 G27 G32 G33 |
| Firmware file | `zxheifi_firmware.bin` | `zxheifi_firmware_esp32.bin` |

### Moving from a NodeMCU to an ESP32

1. On the NodeMCU, go to Admin → Settings → **Download backup**.
2. In the Setup Companion → Flash Firmware, set Device type to **Main unit
   (ESP32)** and flash the ESP32. If the wrong board is plugged in, the flash
   stops and says so.
3. Run the **Setup Wizard**: join the `ZxheiFi-Setup` WiFi and give the same
   answers as before.
4. Open **Configure MikroTik**. The NodeMCU MAC field now holds the ESP32's
   MAC, read while flashing. Run Config so the router reserves the address for
   it.
5. Go to Admin → Settings → **Restore** and pick the backup. Everything comes
   back, including the paired coin boxes.
6. Move the coin acceptor wires to G14 (signal) / G13 (relay) / GPIO25
   (buzzer), or pick other pins in Settings → Coin Slot.

## For developers

- **Firmware layout:** one source tree builds both boards.
  - `firmware/platform.h` maps the differences: web server, FS, HTTPS client,
    mDNS, random, the open-WiFi constant, header collection, pin labels.
  - `pio run -d firmware -e main_esp8266`
  - `pio run -d firmware -e main_esp32`, then `python tools/merge_esp32.py`.
    This produces one image (bootloader + partitions + app) flashed at 0x0.
- `firmware/backup_api.h`:
  - `GET /api/admin/backup` streams the files;
  - `POST /api/admin/restore` is multipart (field `backup`). The upload goes
    to `/restore.tmp`, `firmware/backup_split.h` splits it into `/r_*.tmp`,
    and the files are swapped in from `loop()` right before the restart.
- **Tests:**
  - `common/host_test/test_backup_split.cpp` covers every chunk size,
    escapes, truncation and unknown files;
  - `tools/vendo_test.py` covers backup/restore against the mock.
