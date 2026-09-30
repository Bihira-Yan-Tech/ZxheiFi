"""
build.py - builds both distributable versions of ZxheiFi Setup Companion:

1. Portable: a single .exe (PyInstaller --onefile) that runs from any one
   folder with no installation.
2. Installer: a PyInstaller --onedir build wrapped by Inno Setup into a
   traditional Windows installer (Program Files, Start Menu shortcut,
   uninstaller).

Usage: python build.py [--portable] [--installer]
(no flags = build both)

Requires PyInstaller (`pip install pyinstaller`, already in
requirements.txt) and, for --installer, Inno Setup's ISCC.exe on PATH or
at one of ISCC_CANDIDATES below.
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "assets")
ICON = os.path.join(ASSETS, "icon.ico")
FIRMWARE_BIN = os.path.normpath(os.path.join(HERE, "..", "firmware", "zxheifi_firmware.bin"))
DIST = os.path.join(HERE, "dist")
BUILD = os.path.join(HERE, "build")

APP_NAME = "ZxheiFi Setup Companion"
PORTABLE_NAME = "ZxheiFi-Setup-Companion-Portable"
ONEDIR_NAME = "ZxheiFi-Setup-Companion"

# Inno Setup installs to whichever of these it finds first; ISCC.exe was
# installed outside the conventional Program Files path on this machine
# (see docs/09-changelog.md), so check both.
ISCC_CANDIDATES = [
    r"C:\InnoSetup6\ISCC.exe",
    r"C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    r"C:\Program Files\Inno Setup 6\ISCC.exe",
]


def find_iscc():
    for path in ISCC_CANDIDATES:
        if os.path.isfile(path):
            return path
    found = shutil.which("ISCC.exe") or shutil.which("ISCC")
    return found


def run_pyinstaller(args):
    cmd = [sys.executable, "-m", "PyInstaller", "--noconfirm"] + args
    print("+ " + " ".join(cmd))
    subprocess.run(cmd, check=True, cwd=HERE)


# esptool ships its ESP8266/ESP32 stub-flasher payloads as package data
# (esptool/targets/stub_flasher/*), not Python code - PyInstaller's default
# import analysis won't pick those up on its own (main.py only calls
# esptool._main()/esptool.main(), a pure-code path), so without
# --collect-data the packaged exe connects to the chip fine but then fails
# with "Flasher stub data is missing for ESP8266" the moment it needs to
# actually flash/talk past the initial handshake. See docs/09-changelog.md.
ESPTOOL_DATA_ARGS = ["--collect-data", "esptool"]

# The current firmware ships inside the app, so Flash Firmware has a
# default .bin on any PC (main.py's DEFAULT_FIRMWARE looks for it here).
SUB_FIRMWARE_BIN = os.path.normpath(os.path.join(HERE, "..", "subvendo", "zxheifi_subvendo.bin"))
CHARGING_FIRMWARE_BIN = os.path.normpath(os.path.join(HERE, "..", "charging", "zxheifi_charging.bin"))
FIRMWARE_ARGS = ["--add-data", f"{FIRMWARE_BIN};firmware", "--add-data", f"{SUB_FIRMWARE_BIN};firmware",
                 "--add-data", f"{CHARGING_FIRMWARE_BIN};firmware"]

# The customer GUI too, for Configure MikroTik's Upload GUI Files button
# (main.py's GUI_DIR looks for it here).
GUI_SRC = os.path.normpath(os.path.join(HERE, "..", "mikrotik", "gui"))
GUI_ARGS = ["--add-data", f"{GUI_SRC};gui"]


def build_portable():
    print("\n=== Building portable .exe (--onefile) ===")
    run_pyinstaller([
        "--onefile", "--windowed",
        "--name", PORTABLE_NAME,
        "--icon", ICON,
        "--add-data", f"{ASSETS};assets",
        *ESPTOOL_DATA_ARGS,
        *FIRMWARE_ARGS,
        *GUI_ARGS,
        "main.py",
    ])
    exe = os.path.join(DIST, PORTABLE_NAME + ".exe")
    print(f"Portable build: {exe}")


def build_onedir():
    print("\n=== Building --onedir folder (for the installer) ===")
    run_pyinstaller([
        "--onedir", "--windowed",
        "--name", ONEDIR_NAME,
        "--icon", ICON,
        "--add-data", f"{ASSETS};assets",
        *ESPTOOL_DATA_ARGS,
        *FIRMWARE_ARGS,
        *GUI_ARGS,
        "main.py",
    ])
    folder = os.path.join(DIST, ONEDIR_NAME)
    print(f"Onedir build: {folder}")
    return folder


def build_installer():
    # Always rebuild: reusing an existing onedir folder packaged whatever
    # old build happened to be lying around into the installer.
    build_onedir()

    iscc = find_iscc()
    if not iscc:
        print("ERROR: Inno Setup's ISCC.exe not found (checked PATH and "
              + ", ".join(ISCC_CANDIDATES) + "). Install Inno Setup or add "
              "it to PATH, then re-run with --installer.")
        sys.exit(1)

    print(f"\n=== Building installer with {iscc} ===")
    iss_path = os.path.join(HERE, "installer.iss")
    subprocess.run([iscc, iss_path], check=True, cwd=HERE)
    print(f"Installer build: {os.path.join(DIST, 'ZxheiFi-Setup-Companion-Installer.exe')}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--portable", action="store_true", help="build only the portable .exe")
    parser.add_argument("--installer", action="store_true", help="build only the installer")
    args = parser.parse_args()

    if not os.path.isfile(FIRMWARE_BIN):
        print(f"ERROR: firmware not found at {FIRMWARE_BIN} - compile it first (see docs/05-nodemcu-flash.md).")
        sys.exit(1)
    if not os.path.isfile(CHARGING_FIRMWARE_BIN):
        print(f"ERROR: charging station firmware not found at {CHARGING_FIRMWARE_BIN} - run: pio run -d charging "
              "(see firmware/BUILD_INSTRUCTIONS.md).")
        sys.exit(1)
    if not os.path.isfile(SUB_FIRMWARE_BIN):
        print(f"ERROR: sub vendo firmware not found at {SUB_FIRMWARE_BIN} - run: pio run -d subvendo "
              "(see firmware/BUILD_INSTRUCTIONS.md).")
        sys.exit(1)
    if not os.path.isfile(ICON):
        print(f"ERROR: icon not found at {ICON} - run the logo->ico conversion first.")
        sys.exit(1)

    build_both = not args.portable and not args.installer
    if args.portable or build_both:
        build_portable()
    if args.installer or build_both:
        build_installer()

    print("\nDone. See desktop-app/dist/ for output files.")


if __name__ == "__main__":
    main()
