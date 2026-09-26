# Building the firmware

ZxheiFi has two firmwares:

| Firmware | Source | Output the Setup Companion flashes |
|---|---|---|
| Main unit | `firmware/` | `firmware/zxheifi_firmware.bin` |
| Sub Vendo (v2) | `subvendo/` | `subvendo/zxheifi_subvendo.bin` |

Both include the shared protocol header `common/zx_protocol.h`.

You normally don't need to build anything: both `.bin` files are
committed and bundled in the Setup Companion.

## PlatformIO (recommended)

```bash
pio run -d firmware     # main unit  -> firmware/.pio/build/main_esp8266/firmware.bin
pio run -d subvendo     # sub vendo  -> subvendo/.pio/build/sub_esp8266/firmware.bin
```

Copy each result over the committed `.bin` shown in the table above. Each
folder's `platformio.ini` already sets the board (`nodemcuv2`), ArduinoJson 6
and the `-I ../common` include path. Keep that path relative, because
PlatformIO splits `${PROJECT_DIR}` on the spaces in this repo's path.

## Tests that need no hardware

```bash
python tools/run_host_tests.py     # C++ protocol (zx_protocol.h) on the PC
python tools/test_zx_protocol.py   # Python reference, same vectors
python tools/vendo_test.py         # sub-vendo API contract (mock server)
python tools/regression_test.py    # v1 API contract (mock server)
```

`run_host_tests.py` needs PlatformIO's MinGW compiler, installed once with
`pio pkg install -g -t platformio/toolchain-gccmingw32`.

To run the same protocol vectors on a real board:

```bash
pio run -d common/selftest -t upload --upload-port COM5
python tools/serial_watch.py COM5 --expect "SELFTEST PASSED"
```

This replaces only the app, not the saved data. Reflash the real firmware
afterwards.

## Arduino IDE

1. Install the ESP8266 board package (Boards Manager URL
   `http://arduino.esp8266.com/stable/package_esp8266com_index.json`) and
   ArduinoJson **6.x**.
2. Copy `common/zx_protocol.h` into the sketch folder (`firmware/` or
   `subvendo/`). The IDE doesn't know about `common/`.
3. Open `firmware/nodemcu_firmware.ino` (or `subvendo/subvendo.ino`) and set
   these options:
   - Board: "NodeMCU 1.0 (ESP-12E Module)"
   - Flash Size: "4MB (FS:3MB)"
   - CPU: 80 MHz
4. Sketch → Export compiled Binary.

`firmware/config.h` normally needs no edits. WiFi, MikroTik and admin
credentials come from the Setup Wizard, and prices, speeds and pins come from
the admin panel.

## Size budget

- The main unit must stay under 900 KB (about 577 KB in 2.0.0-dev). The app
  partition is about 1,044,464 bytes.
- The sub vendo is about 370 KB.

## Flashing

Use the Setup Companion's Flash Firmware tab and choose the Device type:
**Main unit** or **Sub Vendo**. Any esptool-based flasher also works: write
the `.bin` at offset 0x0.
