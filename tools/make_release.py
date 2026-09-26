"""
make_release.py - assembles release/ZxheiFi-v<version>/ with three folders:
  Source Code/  the repository snapshot (no build output, no .git)
  Installer/    the installer .exe + every file the app carries, loose
  Portable/     the portable .exe + the same loose files
Run after `python desktop-app/build.py`. The version is read from
desktop-app/main.py's APP_VERSION.
"""
import hashlib
import os
import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION = re.search(r'APP_VERSION = "([^"]+)"', (ROOT / "desktop-app" / "main.py").read_text(encoding="utf-8")).group(1)
REL = ROOT / "release" / f"ZxheiFi-v{VERSION}"
DIST = ROOT / "desktop-app" / "dist"

SRC_SKIP_DIRS = {".git", "release", "dist", "build", "__pycache__", ".pio", ".vscode", ".idea"}
SRC_SKIP_TOP = {"gui"}          # empty leftover folder at the repo root
SRC_SKIP_EXT = {".pyc", ".spec"}


def copy_source(dst):
    for dirpath, dirnames, filenames in os.walk(ROOT):
        rel = Path(dirpath).relative_to(ROOT)
        dirnames[:] = [d for d in dirnames
                       if d not in SRC_SKIP_DIRS and not (rel == Path(".") and d in SRC_SKIP_TOP)]
        for f in filenames:
            if Path(f).suffix in SRC_SKIP_EXT:
                continue
            out = dst / rel / f
            out.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(Path(dirpath) / f, out)


def copy_extras(dst):
    """Everything the app itself carries, as loose files you can reach
    without the app: firmware, portal pages, RouterOS scripts, docs."""
    (dst / "Firmware").mkdir(parents=True)
    shutil.copy2(ROOT / "firmware" / "zxheifi_firmware.bin", dst / "Firmware")
    shutil.copytree(ROOT / "mikrotik" / "gui", dst / "MikroTik GUI (portal files)",
                    ignore=shutil.ignore_patterns("*.md"))
    shutil.copy2(ROOT / "mikrotik" / "gui" / "sounds" / "README.md",
                 dst / "MikroTik GUI (portal files)" / "sounds" / "README.md")
    scripts = dst / "MikroTik Scripts (manual setup)"
    scripts.mkdir()
    for f in (ROOT / "mikrotik").glob("*.rsc"):
        shutil.copy2(f, scripts)
    shutil.copy2(ROOT / "mikrotik" / "import_guide.md", scripts)
    shutil.copytree(ROOT / "docs", dst / "Docs")
    shutil.copy2(ROOT / "LICENSE", dst / "LICENSE.txt")
    for f in ("README.md", "TERMS.md", "THIRD-PARTY-NOTICES.md"):
        shutil.copy2(ROOT / f, dst)


READ_FIRST = """ZxheiFi v{v} - {kind}
==============================================

TAGALOG
-------
{tl}

Laman ng folder na ito:
  {exe}
  Firmware\\zxheifi_firmware.bin          - firmware ng NodeMCU (kasama na rin sa app)
  MikroTik GUI (portal files)\\           - login/status/admin pages + sounds
                                           (ina-upload ng app gamit ang "Upload GUI Files")
  MikroTik Scripts (manual setup)\\       - .rsc files kung ayaw gumamit ng app
  Docs\\                                  - buong dokumentasyon
  LICENSE.txt, THIRD-PARTY-NOTICES.md, TERMS.md, README.md

Pagkatapos i-install/buksan: basahin ang "Guide" tab ng app (may English/Tagalog).
Kung may lumang NodeMCU: i-flash ang bagong firmware AT i-Upload GUI Files ulit.

ENGLISH
-------
{en}

What's in this folder:
  {exe}
  Firmware\\zxheifi_firmware.bin          - NodeMCU firmware (also built into the app)
  MikroTik GUI (portal files)\\           - login/status/admin pages + sounds
                                           (the app uploads them with "Upload GUI Files")
  MikroTik Scripts (manual setup)\\       - .rsc files if you don't want to use the app
  Docs\\                                  - full documentation
  LICENSE.txt, THIRD-PARTY-NOTICES.md, TERMS.md, README.md

After installing/opening: read the app's "Guide" tab (English/Tagalog switch).
Existing NodeMCU: flash the new firmware AND run Upload GUI Files again.

SHA-256
-------
{sums}
"""


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    if REL.exists():
        shutil.rmtree(REL)
    REL.mkdir(parents=True)

    # --- Source Code
    src = REL / "Source Code"
    copy_source(src)

    # --- Installer
    inst = REL / "Installer"
    inst.mkdir()
    inst_exe = DIST / "ZxheiFi-Setup-Companion-Installer.exe"
    shutil.copy2(inst_exe, inst / f"ZxheiFi-Setup-Companion-v{VERSION}-Installer.exe")
    copy_extras(inst)

    # --- Portable
    port = REL / "Portable"
    port.mkdir()
    port_exe = DIST / "ZxheiFi-Setup-Companion-Portable.exe"
    shutil.copy2(port_exe, port / f"ZxheiFi-Setup-Companion-v{VERSION}-Portable.exe")
    copy_extras(port)

    fw = ROOT / "firmware" / "zxheifi_firmware.bin"
    for folder, kind, exe, tl, en in (
        (inst, "INSTALLER", f"ZxheiFi-Setup-Companion-v{VERSION}-Installer.exe - i-double-click para i-install",
         "Para sa PC na gagamitin nang matagal: nag-i-install sa Program Files, may Start Menu\n"
         "shortcut at uninstaller. Mas mabilis magbukas kaysa sa Portable.",
         "For a PC you'll keep using: installs to Program Files with a Start Menu shortcut\n"
         "and an uninstaller. Opens faster than the Portable version."),
        (port, "PORTABLE", f"ZxheiFi-Setup-Companion-v{VERSION}-Portable.exe - buksan agad, walang install",
         "Walang install: isang .exe na pwedeng dalhin sa USB flash drive at buksan sa\n"
         "kahit anong Windows PC. Medyo mas matagal magbukas (mga 10 segundo).",
         "No install: a single .exe you can carry on a USB drive and run on any Windows PC.\n"
         "Takes a little longer to open (about 10 seconds)."),
    ):
        exe_file = next(folder.glob("*.exe"))
        sums = f"  {sha(exe_file)}  {exe_file.name}\n  {sha(fw)}  zxheifi_firmware.bin"
        (folder / "BASAHIN MUNA - READ FIRST.txt").write_text(
            READ_FIRST.format(v=VERSION, kind=kind, exe=exe, tl=tl, en=en, sums=sums),
            encoding="utf-8")

    (REL / "BASAHIN MUNA - READ FIRST.txt").write_text(f"""ZxheiFi v{VERSION}
===============

Source Code\\  - buong source code (firmware, portal, desktop app, docs, tests) -
                ito ang ilalagay sa GitHub. / Full source code - this is what goes on GitHub.
Installer\\    - Windows installer + lahat ng files ng app. / Windows installer + all app files.
Portable\\     - Portable .exe (walang install) + lahat ng files ng app. / Portable .exe + all app files.

License: MIT (c) 2026 ZxheiFi. Inspired by JuanFi by Ivan Julius Alayan
(github.com/ivanalayan15/JuanFi) - not affiliated.
""", encoding="utf-8")

    total = sum(f.stat().st_size for f in REL.rglob("*") if f.is_file())
    for d in (src, inst, port):
        n = sum(1 for f in d.rglob("*") if f.is_file())
        size = sum(f.stat().st_size for f in d.rglob("*") if f.is_file())
        print(f"{d.name:12s} {n:4d} files  {size / 1e6:6.1f} MB")
    print(f"total {total / 1e6:.1f} MB -> {REL}")


if __name__ == "__main__":
    main()
