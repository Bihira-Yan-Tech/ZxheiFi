# ESP32 Main Unit + Backup/Restore - Design (v2, part 3)

- **Date:** 2026-10-02
- **Status:** approved by the owner in brainstorming.
- **Branch:** `v2`.
- **Builds on:** part 1 (Vendos / Sub Vendo) and part 2 (Charging Station).

## Buod (Tagalog)

- **Puwede nang ESP32 DevKit ang main unit.** Iisang firmware source ito para sa dalawang board, at kaya nito hanggang 10 box (NodeMCU: 3).
- **Admin → Settings → Backup:**
  - **I-download** ang lahat (settings, vouchers, subscribers, admins, sales, logs, sessions, at mga box kasama ang keys) bilang isang file.
  - **I-restore** ito sa parehong board o sa bagong board (NodeMCU → ESP32).
  - Hindi kasama ang WiFi at MikroTik API password, dahil sa Setup Wizard iyon nanggagaling.

## 1. Decisions (owner)

| Question | Decision |
|---|---|
| ESP32 board | **ESP32 DevKit** (ESP32-WROOM-32), WiFi |
| Box keys in the backup | **Included** - no re-pairing after a restore; the Admin warns to keep the file private |
| Where | **Admin panel only** |

## 2. ESP32 support

### 2.1 Platform layer

A new file, `firmware/platform.h`, hides the board differences:

| | ESP8266 (NodeMCU) | ESP32 |
|---|---|---|
| WiFi / web server | `ESP8266WiFi`, `ESP8266WebServer` | `WiFi`, `WebServer` → `using ZxWebServer` |
| HTTPS (Telegram) | `BearSSL::WiFiClientSecure` | `WiFiClientSecure` |
| HTTP client | `ESP8266HTTPClient` | `HTTPClient` |
| mDNS | `ESP8266mDNS` (`MDNS.update()`) | `ESPmDNS` (no `update`) → `ZX_MDNS_UPDATE()` |
| File system | `SPIFFS` (FS.h) | `SPIFFS` (SPIFFS.h), `begin(true)` formats on first use |
| Open-WiFi check | `ENC_TYPE_NONE` | `WIFI_AUTH_OPEN` → `ZX_WIFI_OPEN` |
| Random | `RANDOM_REG32` | `esp_random()` → `ZX_RANDOM32()` |
| `collectHeaders` | variadic | array form |

All firmware files include `platform.h` instead of the ESP8266 headers. The ESP8266 build stays byte-for-byte equivalent in behaviour.

### 2.2 Pins

Coin-slot pin labels are board-specific:

| Board | Coin / relay choices | Defaults |
|---|---|---|
| ESP8266 | `D1 D2 D5 D6 D7` | coin D5, relay D7 |
| ESP32 | `G13 G14 G25 G26 G27 G32 G33` | coin **G14**, relay **G13** |

Fixed pins on the ESP32:
- buzzer GPIO25;
- status LED GPIO2 (active-HIGH on the DevKit; active-LOW on the NodeMCU);
- setup button GPIO0 (BOOT).

GPIO25 is the buzzer, so it is not offered for the coin/relay. The ESP32 choices are therefore `G13 G14 G26 G27 G32 G33`.

`coinPinGpio()` knows the labels of the board it runs on. Health and settings report `board` and `pinChoices`, and the Admin builds its dropdowns from `pinChoices`.

Pins of **sub vendos / charging boxes** are unchanged (they are NodeMCUs).

### 2.3 Build and flash

- `firmware/platformio.ini` gets `[env:main_esp32]` (`espressif32`, `esp32dev`, Arduino, `default.csv` partitions). If the app does not fit, `min_spiffs.csv` is used instead.
- `tools/merge_esp32.py` turns the bootloader + partitions + boot_app0 + app into one image, **`firmware/zxheifi_firmware_esp32.bin`**, flashed at 0x0.
- `MAX_SUB_VENDOS` = 10 and `BOARD_NAME` = `"esp32"`.
- **Setup Companion:**
  - Device type **"Main unit (ESP32)"** → the bundled ESP32 image.
  - The flasher and Scan Device pass `--chip esp32` for it (`esp8266` for the others), so a wrong board fails with a clear message.

## 3. Backup / Restore

### 3.1 Content

The backup is one JSON file, **streamed** (never held in RAM):

```json
{"zxheifiBackup":1,"fw":"2.0.0-dev","board":"esp8266","createdAt":1790000000,
 "files":{"config.json":{...},"vouchers.json":[...],"subscribers.json":[...],"admins.json":[...],
          "activity.json":[...],"sales.json":[...],"today.json":{...},"sessions.json":{...},
          "vendos.json":[...],"collections.json":[...]}}
```

- Missing files are simply left out.
- `network.json` is **never** included: WiFi and MikroTik API credentials come from the new board's Setup Wizard.
- The admin password hashes are included (they are salted MD5).
- The box keys are included (owner's decision).

### 3.2 Endpoints (super admin)

**`GET /api/admin/backup`**
- Streams the file with `Content-Disposition: attachment; filename="zxheifi-backup-<date>.json"`.
- Logged as `backup_downloaded`.

**`POST /api/admin/restore`**
- `multipart/form-data`, field `backup`. The body is streamed to `/restore.tmp`, with a limit of 1 MB.
- The admin is checked when the upload starts; unauthorized data is discarded.
- `backup_split.h` then walks the file once and copies each `files` entry, raw, into `/r_<name>.tmp` (string/escape-aware brace counting).
- **Nothing is replaced unless the whole file parses:**
  - the header is checked (`zxheifiBackup == 1`);
  - every value is a complete JSON object or array;
  - only known file names are accepted.
- Then each `/r_<name>.tmp` is renamed over the live file, `restore` is logged, and the reply is `{ok, files:[...], rebootInMs:1500}` before restarting.
- **Errors:**
  - 400 `not_a_backup`, `bad_backup`, `backup_too_large`, `no_file`;
  - 401 / 403 as for every super-admin endpoint.
  - On any error, all temp files are removed.

**After restart.** If the restored coin/relay pins are not valid labels for this board, they are reset to the board defaults and `pins_reset` is logged. This is the NodeMCU → ESP32 migration case.

### 3.3 `firmware/backup_split.h`

Pure C++, no Arduino calls, host-tested:

```cpp
class BackupSplitter {
  // feed(chunk) any number of times, then finish(); callbacks:
  //   onFileStart(name) -> bool accept, onFileData(bytes, n), onFileEnd() -> bool ok
  // state(): Ok / NotABackup / Malformed; header() exposes version/board/fw
};
```

It handles:
- strings with escaped quotes and braces inside;
- nested objects and arrays;
- whitespace;
- chunk boundaries at any byte;
- an unknown key → value skipped;
- truncated input → Malformed.

### 3.4 Admin UI

Settings → **Backup & Restore**:
- **Download backup:** fetch with the admin headers, then save as a blob.
- Warning text: *"Keep this file private - it lets someone take over your coin boxes."*
- **Restore:** a file picker, a confirm dialog naming the file's board, date and version, then upload → "Restoring... the unit restarts" → reload after 20 s.
- Errors are shown in plain words.

### 3.5 Mock

- `GET /api/admin/backup` returns the mock's state in the same shape.
- `POST /api/admin/restore` (multipart) replaces the mock's in-memory state with the same validation.
- Health/settings report `board` and `pinChoices`.

## 4. Testing

1. **Host C++** (`test_backup_split.cpp`):
   - a well-formed backup → every file intact;
   - every chunk size from 1 to the full length gives the same result;
   - escaped quotes and braces inside strings;
   - unknown key skipped;
   - not-a-backup;
   - truncated → Malformed;
   - an unknown file name is rejected by the callback.
2. **Mock** (`vendo_test.py` or a new `backup_test.py`):
   - backup download (super) → JSON shape, no network credentials, `vendos` keys present;
   - staff → 403;
   - restore round-trip: change data, restore the earlier backup, data is back;
   - a garbage file → 400 and nothing changed;
   - not-a-backup → 400;
   - `pinChoices` per board.
3. `regression_test.py` 103/103 and `vendo_test.py` all green.
4. `pio run -e main_esp8266` and `-e main_esp32` with no new warnings; the merged ESP32 image is produced.
5. Browser: the Backup section (download, restore flow with a confirm) and the pin dropdowns from `pinChoices`.
6. **Hardware (owner, later):** flash an ESP32 DevKit, run the wizard, restore a NodeMCU backup, then test a coin and a voucher.

## 5. Out of scope

Ethernet (WT32-ETH01), OTA updates, automatic or scheduled backups, encrypted backups.
