# Building zxheifi_firmware.bin

## Prerequisites
1. Install Arduino IDE (or use PlatformIO)
2. Install ESP8266 board package:
   - In Arduino IDE: Files → Preferences → Additional Boards Manager URLs: 
     http://arduino.esp8266.com/stable/package_esp8266com_index.json
   - Tools → Board → Boards Manager → Install "esp8266 by ESP8266 Community"
3. Install required libraries via Library Manager:
   - ArduinoJson by Benoit Blanchon (v6.x)
   - ESP8266WiFi, ESP8266WebServer, ESP8266mDNS, DNSServer, FS (all
     built-in, bundled with the esp8266 board package - no separate
     install needed)

## Build Steps
1. Open `firmware/nodemcu_firmware.ino` in Arduino IDE
2. Select Board: "NodeMCU 1.0 (ESP-12E Module)"
   - Flash Size: "4M (3M SPIFFS)"
   - CPU Frequency: "80 MHz"
   - Upload Speed: "115200"
   - Port: [your COM port]
3. `firmware/config.h` mostly does NOT need editing before compiling —
   as of the [Setup Wizard](../docs/12-setup-wizard.md), WiFi/MikroTik/
   admin credentials are entered on first boot from a phone/laptop
   instead. Still worth editing before compiling: TIER pricing/limits
   if you want different starting values, GPIO pin assignments if your
   wiring differs, `NODEMCU_MDNS_NAME`/`SETUP_AP_SSID` if you want a
   different device name.
4. Click Verify (checkmark) to compile
   - The .bin file will be generated in your sketch folder:
     `{SketchFolder}/firmware/nodemcu_firmware.ino.bin`
5. Rename to `zxheifi_firmware.bin` for clarity

## Size Verification
After compilation, check the output window for:
```
Sketch uses XXX bytes (YY%) of program storage space.
```
Ensure XXX < 900,000 bytes (<900KB) — see `config.h`'s SIZE CONSTRAINTS
section for why this was raised from an original 500KB round number
(the real ceiling is the "4M (3M SPIFFS)" board layout's ~1,044,464-byte
app partition).

## Flashing the Binary
Use NodeMCU-PyFlasher or ESP8266Flasher:
1. Connect NodeMCU via USB
2. Select COM port
3. Firmware File: `zxheifi_firmware.bin`
4. Flash Mode: dio
5. Baud: 115200
6. Erase Flash: Yes
7. Click Flash NodeMCU
8. Press RST button on NodeMCU after flashing

## Troubleshooting
- **Compilation errors**: Ensure ArduinoJson v6.x is installed
- **Flash failures**: Check USB cable (must support data, not just power)
- **WiFi not connecting**: Verify SSID/password and that MikroTik AP is broadcasting
- **No API response**: Confirm MikroTik API user/password and port 8728 accessible

## Alternative: PlatformIO
If using PlatformIO:
1. Copy `platformio.ini` from template (if provided) or create:
   ```
   [env:nodemcuv2]
   platform = espressif8266
   board = nodemcuv2
   framework = arduino
   lib_deps = 
     ArduinoJson
   build_flags = 
     -D CORE_DEBUG_LEVEL=0
   ```
2. Run `pio run` to build
3. Binary at `.pio/build/nodemcuv2/firmware.bin`

## Notes
- The .ino source is provided for reference and modification
- End users should flash the pre-compiled .bin file
- GUI files (`login.html`, etc.) must be uploaded separately to MikroTik Hotspot directory