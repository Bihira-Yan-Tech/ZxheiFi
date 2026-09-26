# Third-Party Notices

ZxheiFi's own code is MIT-licensed (see [LICENSE](LICENSE)). It uses or
bundles the following open-source components, each under its own license.

## Customer portal (`mikrotik/gui/`)

| Component | Use | License |
|---|---|---|
| [QRCode.js](https://github.com/davidshimjs/qrcodejs) (`qrcode.min.js`) — Copyright (c) 2012 davidshimjs | Voucher and share QR codes | MIT |
| Sound files in `sounds/` | Portal sounds | Original, generated for ZxheiFi (voice prompt made with Windows text-to-speech); MIT like the rest of ZxheiFi |

## NodeMCU firmware (`firmware/`)

| Component | License |
|---|---|
| [ESP8266 Arduino core](https://github.com/esp8266/Arduino) | LGPL-2.1 |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) — Copyright (c) Benoit Blanchon | MIT |

The compiled `zxheifi_firmware.bin` links the ESP8266 Arduino core (LGPL-2.1).
Its source is available at the link above; ZxheiFi's firmware source is in
this repository, so the binary can be rebuilt and relinked.

## Setup Companion (`desktop-app/`)

| Component | License |
|---|---|
| [esptool](https://github.com/espressif/esptool) | **GPL-2.0-or-later** |
| [CustomTkinter](https://github.com/TomSchimansky/CustomTkinter) | MIT |
| [pySerial](https://github.com/pyserial/pyserial) | BSD-3-Clause |
| [Pillow](https://github.com/python-pillow/Pillow) | MIT-CMU (HPND) |
| [psutil](https://github.com/giampaolo/psutil) | BSD-3-Clause |
| [Python](https://www.python.org/) runtime (bundled by PyInstaller) | PSF License |
| [PyInstaller](https://pyinstaller.org/) bootloader | GPL-2.0 with a bootloader exception (allows distributing the built app under any license) |
| [Inno Setup](https://jrsoftware.org/isinfo.php) (builds the installer) | Inno Setup License |

**About esptool (GPL):** the Windows executables include esptool, which is
used to flash the NodeMCU. Distributing those executables therefore also
means following the GPL for that part: the complete source of the
Setup Companion is in this repository (`desktop-app/`), and esptool's source
is at the link above. ZxheiFi's own source remains MIT-licensed; the GPL
applies to the combined executables as distributed.

Full license texts are in each project's repository at the links above.

---

### QRCode.js license (MIT)

```
The MIT License (MIT)
---------------------
Copyright (c) 2012 davidshimjs

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```
