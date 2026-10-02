# ESP32 Main Unit + Backup/Restore Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The main-unit firmware also builds for an ESP32 DevKit (up to 10 boxes), and the Admin can download a full backup and restore it on the same or a different board.

**Architecture:**
- `firmware/platform.h` maps the ESP8266/ESP32 differences (web server, FS, HTTPS, mDNS, random, open-WiFi check, header collection, pins), so one source tree builds both.
- Backup is streamed straight from the files.
- Restore is uploaded to a temp file, then split by a pure-C++ `BackupSplitter` (host-tested) into per-file temp files. They are swapped in only if the whole backup is valid, then the unit reboots.

**Tech Stack:**
- PlatformIO `espressif8266` + `espressif32` (Arduino), ArduinoJson 6.
- esptool `merge_bin` for the single ESP32 image.
- Python mock/tests, vanilla JS, CustomTkinter.

**Spec:** [docs/superpowers/specs/2026-10-02-esp32-main-backup-restore-design.md](../specs/2026-10-02-esp32-main-backup-restore-design.md)

## Global Constraints

**Pins**

| Board | Coin / relay choices | Defaults |
|---|---|---|
| ESP8266 | `D1 D2 D5 D6 D7` | coin D5, relay D7 |
| ESP32 | `G13 G14 G26 G27 G32 G33` | coin G14, relay G13 |

ESP32 fixed pins: buzzer GPIO25, LED GPIO2 (active-HIGH), setup button GPIO0.

**Boards and images**
- `BOARD_NAME` is `"esp8266"` or `"esp32"`; `MAX_SUB_VENDOS` is 3 or 10.
- The ESP32 image is `firmware/zxheifi_firmware_esp32.bin`, flashed at 0x0.

**Backup format**

```json
{"zxheifiBackup":1,"fw":...,"board":...,"createdAt":...,"files":{"<name>":<json>}}
```

- Names: `config.json vouchers.json subscribers.json admins.json activity.json sales.json today.json sessions.json vendos.json collections.json`.
- `network.json` is never included.
- Restore is `multipart/form-data`, field `backup`, at most 1 048 576 bytes.
- Restore errors: `not_a_backup`, `bad_backup`, `backup_too_large`, `no_file`.

**Never break**
- `regression_test.py` stays 103/103; `vendo_test.py` and the host tests stay green.
- No new compiler warnings on either board.

## File map

| File | Change |
|---|---|
| `firmware/platform.h` | new: board abstraction |
| `firmware/backup_split.h` | new: pure-C++ streaming splitter |
| `common/host_test/test_backup_split.cpp` | new |
| `firmware/backup_api.h` | new: `/api/admin/backup` + `/api/admin/restore`, pin reset after restore |
| `firmware/*.h`, `nodemcu_firmware.ino` | include `platform.h`; use `ZxWebServer`, `ZX_*` macros, board pin labels |
| `firmware/config.h` | board-specific pins / LED polarity |
| `firmware/platformio.ini` | `[env:main_esp32]` |
| `tools/merge_esp32.py` | new: builds the merged image |
| `tools/mock_server.py`, `tools/backup_test.py` | mirror + tests |
| `mikrotik/gui/{admin.html, script.js}` | pins from `pinChoices`, Backup & Restore section |
| `desktop-app/{main.py, flasher.py, build.py}`, `tools/make_release.py` | Main unit (ESP32) device type, chip per device |
| `desktop-app/guide_content.py`, docs | EN/TL Guide, docs/16, changelog, README, BUILD_INSTRUCTIONS |

---

### Task 1: `BackupSplitter` + host tests

**Files:**
- Create: `firmware/backup_split.h`
- Create: `common/host_test/test_backup_split.cpp`

**Interfaces - Produces** (`namespace zxb`)

```cpp
class BackupSplitter {
public:
  enum State { Running, Ok, NotABackup, Malformed };
  struct Sink { virtual bool fileStart(const char* name) = 0; virtual void fileData(const char* p, size_t n) = 0;
                virtual bool fileEnd() = 0; virtual ~Sink() {} };
  explicit BackupSplitter(Sink& sink);
  void feed(const char* p, size_t n);
  State finish();
  State state() const;
  long version() const;            // zxheifiBackup
  const char* board() const;       // "" if absent
  uint16_t fileCount() const;
};
```

- A top-level object is required, with `zxheifiBackup` == 1 (a number) somewhere at the top level; otherwise `NotABackup`.
- `files` must be an object of `name: object|array`.
- Values are passed through raw (the exact bytes).
- A `fileStart` returning false (an unknown name) makes the result `Malformed`.
- Max name length 32.

**Tests:**
- round trip of a sample backup (exact bytes per file);
- every chunk size 1..len gives the same output;
- escaped `\"` and `}` inside strings;
- an unknown top-level key (string / number / object) is skipped;
- header after `files` still accepted;
- missing `zxheifiBackup` → `NotABackup`;
- a version of 2 → `NotABackup`;
- truncated → `Malformed`;
- a file value that's a string → `Malformed`;
- the sink rejects a name → `Malformed`;
- empty `files` → `Ok` with 0 files.

- [ ] Write the tests, run `python tools/run_host_tests.py` (FAIL), implement, run (PASS), commit.

### Task 2: Platform layer + ESP32 build

**Files:**
- Create: `firmware/platform.h`, `tools/merge_esp32.py`
- Modify: `firmware/platformio.ini` and all firmware sources

**Changes:**
- `platform.h`:
  - includes per board;
  - `using ZxWebServer = ESP8266WebServer | WebServer`;
  - `ZxSecureClient`;
  - `ZX_WIFI_OPEN`, `ZX_MDNS_UPDATE()`, `ZX_FS_BEGIN()`;
  - `ZX_COLLECT_HEADERS(server)`;
  - `zxPinGpio(label)` and `zxPinChoices()` (JSON-able list);
  - `ZX_LED_ON` level.
- `config.h`: per-board `PIN_*` defaults; `DEFAULT_COIN_PIN` / `DEFAULT_RELAY_PIN` labels.
- `coinPinGpio` → `zxPinGpio`. String defaults "D5"/"D7" → the `DEFAULT_*` macros.
- The settings handler validates with `zxPinGpio`.
- The vendo (box) pin validation keeps the ESP8266 box rules (`boxPinAllowed`).
- `platformio.ini` gains:

  ```ini
  [env:main_esp32]
  platform = espressif32
  board = esp32dev
  framework = arduino
  lib_deps = ArduinoJson@^6.21.0
  build_flags = -I ../common
  ```

  It keeps `[platformio] default_envs = main_esp8266`.
- `merge_esp32.py` runs `esptool merge_bin` (bootloader 0x1000, partitions 0x8000, boot_app0 0xe000, app 0x10000) and writes `firmware/zxheifi_firmware_esp32.bin`.

**Steps:**
- [ ] Run `pio run -d firmware -e main_esp8266`: SUCCESS, no new warnings, same behaviour; the test suites stay green.
- [ ] Run `pio run -d firmware -e main_esp32`, then `python tools/merge_esp32.py`: SUCCESS, no warnings; the image is written.
- [ ] Commit.

### Task 3: Backup/Restore API (firmware + mock) + tests

**Files:**
- Create: `firmware/backup_api.h`, `tools/backup_test.py`
- Modify: `nodemcu_firmware.ino`, `gui_handler.h` (health/settings: `board`, `pinChoices`), `tools/mock_server.py`

**Firmware:**
- Backup is streamed with `sendContent` (header, then each existing file's raw content).
- Restore:
  - the upload is handled through `server.on(path, HTTP_POST, onDone, onUpload)`;
  - auth happens at `UPLOAD_FILE_START`;
  - the data goes to `/restore.tmp`, with a size cap;
  - in `onDone`, the file is split into `/r_<name>.tmp`. On `Ok`, each is renamed over its target, then the reply is sent and the unit restarts after 1.5 s. Otherwise the temp files are cleaned up.
- After boot, `adminAPI.loadSettings` resets pins whose label is invalid for the board and logs `pins_reset`.

**Mock:**
- The same endpoints over the in-memory state.
- Multipart is parsed with the `email` package.
- Health and settings report `board` / `pinChoices`.

**Tests** (`backup_test.py`, fresh mock per test):
- a super admin downloads → shape, the files present, no `network.json` / API password, vendo keys present;
- staff → 403, no auth → 401;
- round trip: back up, then change settings, vouchers and a vendo, restore → everything back, `ok` + files list;
- garbage → 400 `bad_backup`, state unchanged;
- valid JSON that isn't a backup → 400 `not_a_backup`;
- no file → 400 `no_file`;
- more than 1 MB → 400 `backup_too_large`;
- health carries `board` + `pinChoices`.

- [ ] Write the tests, run (FAIL), implement, run all suites (PASS).
- [ ] Build both boards.
- [ ] Commit.

### Task 4: Portal

**Files:** `mikrotik/gui/admin.html`, `mikrotik/gui/script.js`

- **Coin Slot dropdowns** are built from `pinChoices` (they keep a saved value even if it's unknown, shown as "(not valid on this board)").
- **Backup & Restore section:**
  - Download → blob save;
  - Restore → file picker → read the header client-side (board / fw / date) for the confirm text → upload `FormData` with the admin headers → "Restoring..." → reload after 20 s;
  - errors are mapped to plain words.

- [ ] Run `node --check`.
- [ ] Browser check on the mock (download, restore with a confirm, pin dropdowns).
- [ ] Commit.

### Task 5: Setup Companion, Guide, docs, release

**Files:** `desktop-app/main.py`, `flasher.py`, `build.py`, `guide_content.py`, `tools/make_release.py`, docs

**App:**
- Device type "Main unit (ESP32)" with `DEFAULT_ESP32_FIRMWARE`.
- `FlashJob` / `DeviceProbe` take `chip` ("esp8266" | "esp32").
- A chip mismatch shows a clear message.
- `build.py` bundles the ESP32 image; the release script ships it.

**Docs:**
- An EN/TL Guide section "ESP32 Main Unit & Backup".
- `docs/16-esp32-and-backup.md`.
- Changelog, README, BUILD_INSTRUCTIONS, masterplan status.

- [ ] Run `py_compile`, the device-type test and `test_configurator`.
- [ ] Commit.

### Task 6: Full audit + builds

- [ ] Run all suites.
- [ ] Run `pio run` for both main boards, the sub vendo, charging and the selftest.
- [ ] Review: restore atomicity, auth, size limits, heap, ESP32 API differences.
- [ ] Clean-build the exes, make the release, update progress + memory, push `v2`.
