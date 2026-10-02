#!/usr/bin/env python3
"""
merge_esp32.py - turns the ESP32 build (bootloader + partition table +
boot_app0 + app) into ONE image that is flashed at 0x0, like the NodeMCU's
.bin - so the Setup Companion flashes both boards the same way.

    pio run -d firmware -e main_esp32
    python tools/merge_esp32.py
    -> firmware/zxheifi_firmware_esp32.bin
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "firmware" / ".pio" / "build" / "main_esp32"
OUT = ROOT / "firmware" / "zxheifi_firmware_esp32.bin"
BOOT_APP0 = (Path.home() / ".platformio" / "packages" / "framework-arduinoespressif32" / "tools" / "partitions"
             / "boot_app0.bin")


def main():
    parts = [("0x1000", BUILD / "bootloader.bin"), ("0x8000", BUILD / "partitions.bin"),
             ("0xe000", BOOT_APP0), ("0x10000", BUILD / "firmware.bin")]
    missing = [str(p) for _, p in parts if not p.is_file()]
    if missing:
        print("Missing (build first: pio run -d firmware -e main_esp32):\n  " + "\n  ".join(missing))
        sys.exit(1)
    args = []
    for offset, path in parts:
        args += [offset, str(path)]
    import esptool
    major = int(esptool.__version__.split(".")[0])
    command = "merge-bin" if major >= 5 else "merge_bin"
    opts = ["--flash-mode", "dio", "--flash-freq", "40m", "--flash-size", "4MB"] if major >= 5 else \
           ["--flash_mode", "dio", "--flash_freq", "40m", "--flash_size", "4MB"]
    cmd = [sys.executable, "-m", "esptool", "--chip", "esp32", command, "-o", str(OUT)] + opts + args
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode:
        print(result.stdout + result.stderr)
        sys.exit(result.returncode)
    print(f"Wrote {OUT} ({OUT.stat().st_size:,} bytes) - flash it at 0x0")


if __name__ == "__main__":
    main()
