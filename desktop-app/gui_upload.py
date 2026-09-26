"""
gui_upload.py - the "Upload GUI Files" button: puts mikrotik/gui/ into the
router's hotspot folder.

This used to be a manual Winbox drag-and-drop, and it went wrong in every
way it could on the first real hAP lite test: the folder didn't exist
(Config creates the hotspot profile but RouterOS only generates its
default page set through its own Hotspot Setup), the docs named the
wrong folder (flash/hotspot vs hotspot depends on the board), and a
hand-made folder lacked MikroTik's own rlogin.html/alogin.html/errors.txt.

So this:
  1. reads the folder from the hotspot profile Config created (never
     guesses it),
  2. runs RouterOS's own `/ip hotspot reset-html` if MikroTik's default
     files aren't there yet,
  3. uploads the GUI over FTP (RouterOS's API can't carry binary files
     like logo.png) - turning the router's FTP service on just for the
     upload if it's off, and back off afterwards,
  4. checks every file actually landed with the right size.

Kept free of Tk code so test_configurator.py can run it against fakes.
"""
import ftplib
import os
import time

from mikrotik_client import RouterOSError

HOTSPOT_SERVER = "zxheifi-hotspot"   # mikrotik_commands.hotspot()
HOTSPOT_PROFILE = "zxheifi-hs"
# Not part of the served GUI. "hotspot" is skipped because people make a
# local copy by that name while uploading by hand - it goes stale.
SKIP_DIRS = {"hotspot", "__pycache__"}
SKIP_EXTS = {".md"}


class UploadError(Exception):
    pass


def gui_files(gui_dir):
    """[(relative path with /, full path)] of everything to upload."""
    out = []
    for root, dirs, files in os.walk(gui_dir):
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS and not d.startswith("."))
        for name in sorted(files):
            if name.startswith(".") or os.path.splitext(name)[1].lower() in SKIP_EXTS:
                continue
            full = os.path.join(root, name)
            out.append((os.path.relpath(full, gui_dir).replace(os.sep, "/"), full))
    return out


def _api(label, fn):
    """Runs one router API call. A busy hAP lite can take several seconds
    to answer (the first real upload died on a bare 'TimeoutError' while
    reading /ip/service), so: one retry, then a clear error."""
    for attempt in (1, 2):
        try:
            return fn()
        except RouterOSError:
            raise
        except OSError as exc:
            if attempt == 2:
                raise UploadError(f"The router didn't answer while {label} ({exc}). It may be busy - "
                                  "check Winbox > System > Resources (CPU load), wait a minute, and try again.")
            time.sleep(2)


def _file_row(client, name):
    try:
        rows = _api(f"checking {name}", lambda: client.print("/file", {"name": name}))
    except RouterOSError:
        return None
    return rows[0] if rows else None


def hotspot_folder(client):
    try:
        rows = _api("reading the hotspot profile",
                    lambda: client.print("/ip/hotspot/profile", {"name": HOTSPOT_PROFILE}))
    except RouterOSError as exc:
        raise UploadError(f"Could not read the hotspot profile ({exc}).")
    if not rows:
        raise UploadError("There's no ZxheiFi hotspot on this router yet - run Config (with Hotspot "
                          "checked) first, then Upload GUI Files.")
    return (rows[0].get("html-directory") or "hotspot").strip("/")


def ensure_default_files(client, folder, log, wait_seconds=10):
    if _file_row(client, f"{folder}/rlogin.html"):
        log(f"  MikroTik's own hotspot pages are already in '{folder}'.")
        return
    log(f"  Creating '{folder}' with MikroTik's default hotspot pages (reset-html)...")
    ok, _, error = _api("creating the default pages",
                        lambda: client.run(["/ip/hotspot/reset-html", f"=numbers={HOTSPOT_SERVER}"]))
    if not ok:
        raise UploadError(f"reset-html failed: {error}")
    deadline = time.time() + wait_seconds
    while time.time() < deadline:
        if _file_row(client, f"{folder}/rlogin.html"):
            return
        time.sleep(0.5)
    raise UploadError(f"RouterOS didn't create '{folder}' - check Winbox > Files.")


def _ftp_service(client):
    try:
        rows = _api("checking the FTP service", lambda: client.print("/ip/service", {"name": "ftp"}))
    except RouterOSError:
        return None
    return rows[0] if rows else None


def _size(value):
    try:
        return int(value)
    except (TypeError, ValueError):
        return None   # older RouterOS reports e.g. "14.9KiB" - skip the size check then


def upload_gui(client, host, user, password, gui_dir, log, ftp_factory=ftplib.FTP):
    """Returns the number of files uploaded. Raises UploadError."""
    files = gui_files(gui_dir)
    if not any(rel == "login.html" for rel, _ in files):
        raise UploadError(f"No login.html in {gui_dir} - is this the GUI folder?")

    folder = hotspot_folder(client)
    log(f"  Hotspot folder on this router: '{folder}'")
    ensure_default_files(client, folder, log)

    log("  Checking the router's FTP service...")
    service = _ftp_service(client)
    reenable_off = False
    if service and service.get("disabled") == "true":
        log("  Turning on the router's FTP service for the upload (turned off again after)...")
        ok, _, error = _api("turning on FTP", lambda: client.run(
            ["/ip/service/set", f"=.id={service['.id']}", "=disabled=no"]))
        if not ok:
            raise UploadError(f"Could not turn on FTP: {error}")
        reenable_off = True
    try:
        _ftp_upload(host, user, password, folder, files, log, ftp_factory)
    finally:
        if reenable_off:
            try:
                _api("turning FTP back off", lambda: client.run(
                    ["/ip/service/set", f"=.id={service['.id']}", "=disabled=yes"]))
                log("  FTP service turned back off.")
            except (UploadError, RouterOSError) as exc:
                log(f"  [!] Couldn't turn FTP back off ({exc}) - do it in Winbox > IP > Services > ftp.")

    # A hAP lite writes to slow flash in the background: right after the
    # upload its file list can still show the old size (or none) for a few
    # seconds - the first real run "failed" 5 of 7 files that were fine.
    log("  Checking the files on the router...")
    pending = {rel: os.path.getsize(full) for rel, full in files}
    seen = {}
    for attempt in range(8):
        if attempt:
            time.sleep(2)
        for rel in list(pending):
            row = _file_row(client, f"{folder}/{rel}")
            actual = _size(row.get("size")) if row else None
            seen[rel] = "missing" if not row else (row.get("size") or "?")
            if row and (actual is None or actual == pending[rel]):
                del pending[rel]
        if not pending:
            break
    if pending:
        detail = ", ".join(f"{rel} (router shows {seen[rel]}, expected {size:,})" for rel, size in pending.items())
        raise UploadError(f"Uploaded, but these don't match on the router after 15s: {detail}. "
                          "Click Upload GUI Files again.")
    log(f"  Verified: all {len(files)} files are in '{folder}' with the right sizes.")
    return len(files)


def _ftp_upload(host, user, password, folder, files, log, ftp_factory):
    ftp = ftp_factory()
    log(f"  Connecting to the router's FTP ({host}:21)...")
    try:
        ftp.connect(host, 21, timeout=30)
        ftp.login(user, password)
    except (OSError, ftplib.Error) as exc:
        raise UploadError(f"FTP connection to {host} failed ({exc}). In Winbox > IP > Services, "
                          "check that 'ftp' isn't limited to other addresses.")
    try:
        made = set()
        for rel, full in files:
            remote = f"{folder}/{rel}"
            parts = remote.split("/")[:-1]
            for i in range(1, len(parts) + 1):
                d = "/".join(parts[:i])
                if d not in made:
                    try:
                        ftp.mkd(d)
                    except ftplib.error_perm:
                        pass   # already exists
                    made.add(d)
            current = remote
            with open(full, "rb") as fh:
                ftp.storbinary(f"STOR {remote}", fh)
            log(f"  [ok] {remote} ({os.path.getsize(full):,} bytes)")
    except (OSError, ftplib.Error) as exc:
        where = locals().get("current", "the first file")
        raise UploadError(f"Upload stopped at {where}: {exc}")
    finally:
        try:
            ftp.quit()
        except (OSError, ftplib.Error):
            ftp.close()
