"""
flasher.py - wraps esptool to flash the NodeMCU firmware, and a small
serial-console reader for watching boot logs right after flashing.

The serial reader uses the same DTR/RTS reset pulse validated against
real ESP8266 hardware this project's own dev session (equivalent to
esptool's own auto-reset sequence), so "watch it boot" works without
needing a second tool.
"""
import os
import re
import subprocess
import sys
import threading
import time

import serial
import serial.tools.list_ports


def _is_usb(port_info):
    return bool(port_info.vid) or "USB" in (port_info.hwid or "").upper()


def _label(p):
    desc = re.sub(r"\s*\(COM\d+\)\s*$", "", p.description or "").strip()
    if "BTHENUM" in (p.hwid or "").upper() or "bluetooth" in desc.lower():
        return f"{p.device} - Bluetooth (not a NodeMCU)"
    return f"{p.device} - {desc}" if desc and desc != "n/a" else p.device


def list_com_ports():
    """[(device, label)] with USB serial ports (where a NodeMCU shows up)
    first. Bluetooth "Standard Serial over Bluetooth link" ports used to be
    listed - and auto-probed - first on some PCs, which sat on "Detecting..."
    for up to a minute."""
    ports = sorted(serial.tools.list_ports.comports(),
                   key=lambda p: (not _is_usb(p), p.device))
    return [(p.device, _label(p)) for p in ports]


def _kill_tree(proc):
    # A packaged (onefile) exe is a bootloader + the real child process;
    # killing only the parent leaves the child holding the COM port.
    if proc.poll() is not None:
        return
    if os.name == "nt":
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)], capture_output=True,
                       creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    else:
        proc.kill()


# esptool is run as a real subprocess (not called in-process) so its output
# can be streamed line-by-line for live progress/log display. In a normal
# `python main.py` dev run, sys.executable is a real python.exe, so
# `-m esptool` works - but in a PyInstaller-packaged .exe, sys.executable
# IS the packaged app itself (there's no separate Python on the end user's
# machine to run `-m esptool` against). ESPTOOL_WORKER_ARG makes the app
# re-invoke itself with this sentinel instead; main.py checks for it before
# touching Tkinter and just runs esptool.main() in-process, then exits -
# turning the single packaged exe into its own esptool worker.
ESPTOOL_WORKER_ARG = "--esptool-worker"


def _self_invoke_prefix():
    if getattr(sys, "frozen", False):
        return [sys.executable]
    main_py = os.path.join(os.path.dirname(os.path.abspath(__file__)), "main.py")
    return [sys.executable, main_py]


_PERCENT_RE = re.compile(r"(\d{1,3})\s*%")


class FlashJob:
    """Runs esptool in a background thread, streaming log lines and
    progress (0-100, or None if indeterminate) to the given callbacks."""

    def __init__(self, port, firmware_path, on_line, on_progress, on_done, baud=460800, erase_all=False,
                 on_mac=None):
        # esptool prints the chip's MAC while connecting, so a flash also
        # identifies the device - no separate scan needed.
        self.on_mac = on_mac
        self.port = port
        self.firmware_path = firmware_path
        # A plain flash only rewrites the firmware area - the device's saved
        # data (wizard settings, admin accounts, vouchers, logs) lives in its
        # own flash region and survives. erase_all wipes the whole chip
        # first, so it boots like a brand-new unit into Setup Mode.
        self.erase_all = erase_all
        self.on_line = on_line
        self.on_progress = on_progress
        self.on_done = on_done
        self.baud = baud
        self._proc = None
        self._thread = None

    def start(self):
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        cmd = _self_invoke_prefix() + [ESPTOOL_WORKER_ARG, "-c", "esp8266", "-p", self.port,
                                        "-b", str(self.baud), "write-flash"]
        if self.erase_all:
            cmd.append("--erase-all")
        cmd += ["0x0", self.firmware_path]
        try:
            self._proc = subprocess.Popen(
                cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, bufsize=1, universal_newlines=True,
            )
            for line in self._proc.stdout:
                line = line.rstrip("\n")
                if not line:
                    continue
                self.on_line(line)
                mac = _MAC_RE.search(line.strip())
                if mac and self.on_mac:
                    self.on_mac(mac.group(1).lower())
                    self.on_mac = None  # once is enough
                m = _PERCENT_RE.search(line)
                if m:
                    self.on_progress(int(m.group(1)))
            code = self._proc.wait()
            self.on_done(code == 0, code)
        except Exception as exc:  # surfaced to the log pane, not swallowed
            self.on_line(f"[flasher error] {exc}")
            self.on_done(False, -1)


_MAC_RE = re.compile(r"^MAC:\s*([0-9a-fA-F:]{17})", re.MULTILINE)
_IP_RE = re.compile(r"IP:\s*(\d{1,3}(?:\.\d{1,3}){3})")


class DeviceProbe:
    """Reads the connected NodeMCU's real MAC address via esptool - works
    even on a device with no WiFi configured yet, since esptool talks
    straight to the ESP8266 bootloader over serial (same reset mechanism
    FlashJob uses), not to the sketch. esptool's own reset at the end of
    `read-mac` leaves the board booting normally, so this then briefly
    watches the serial console for the firmware's own
    "WiFi connected, IP: ..." line (see nodemcu_firmware.ino) in case the
    device already has a network connection - that part is best-effort
    and simply reports no IP if the device isn't on a network yet.
    """

    TIMEOUT = 45

    def __init__(self, port, on_status, on_result, ip_watch_seconds=6):
        self.port = port
        self.on_status = on_status
        self.on_result = on_result
        self.ip_watch_seconds = ip_watch_seconds
        self._thread = None
        self._proc = None
        self._cancelled = False

    def start(self):
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def cancel(self):
        """Stops a probe the user no longer wants (they picked another port)
        and frees its COM port. Its callbacks are not called afterwards."""
        self._cancelled = True
        if self._proc:
            # taskkill takes a few seconds - don't freeze the window for it.
            threading.Thread(target=_kill_tree, args=(self._proc,), daemon=True).start()

    def _run(self):
        self.on_status(f"Detecting device on {self.port}...")
        output = ""
        try:
            self._proc = subprocess.Popen(
                _self_invoke_prefix() + [ESPTOOL_WORKER_ARG, "-c", "esp8266", "-p", self.port, "read-mac"],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
            try:
                output, _ = self._proc.communicate(timeout=self.TIMEOUT)
            except subprocess.TimeoutExpired:
                # subprocess.run() used to wait forever here: it killed only
                # the exe's bootloader, and the real child kept the pipe open.
                _kill_tree(self._proc)
                output = ""
                if not self._cancelled:
                    self.on_status(f"No answer from {self.port} after {self.TIMEOUT}s - is this the NodeMCU's port?")
                    self.on_result(None, None)
                return
        except Exception as exc:
            if not self._cancelled:
                self.on_status(f"Detection failed: {exc}")
                self.on_result(None, None)
            return
        if self._cancelled:
            return

        matches = _MAC_RE.findall(output or "")
        if not matches:
            self.on_status("No device responded - check the port/cable.")
            self.on_result(None, None)
            return
        mac = matches[-1].lower()

        self.on_status(f"MAC {mac} - checking for a live IP...")
        ip = self._watch_for_ip()
        if self._cancelled:
            return
        if ip:
            self.on_status(f"MAC {mac}  |  IP {ip}")
        else:
            self.on_status(f"MAC {mac}  |  no WiFi IP yet (unconfigured or off-network)")
        self.on_result(mac, ip)

    def _watch_for_ip(self):
        try:
            ser = serial.Serial(self.port, 115200, timeout=0.3)
        except serial.SerialException:
            return None
        try:
            deadline = time.time() + self.ip_watch_seconds
            buf = b""
            while time.time() < deadline and not self._cancelled:
                chunk = ser.read(256)
                if not chunk:
                    continue
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    m = _IP_RE.search(line.decode("utf-8", errors="replace"))
                    if m:
                        return m.group(1)
            return None
        finally:
            ser.close()


class SerialMonitor:
    """Resets the board (DTR/RTS pulse) and streams serial output to a
    callback until stop() is called."""

    def __init__(self, port, on_line, baud=115200):
        self.port = port
        self.baud = baud
        self.on_line = on_line
        self._stop = threading.Event()
        self._thread = None

    def start(self):
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self):
        self._stop.set()

    def _run(self):
        try:
            ser = serial.Serial(self.port, self.baud, timeout=0.2)
        except serial.SerialException as exc:
            self.on_line(f"[serial error] {exc}")
            return
        try:
            ser.setDTR(False)
            ser.setRTS(True)
            time.sleep(0.1)
            ser.setRTS(False)
            time.sleep(0.1)
            buf = b""
            while not self._stop.is_set():
                chunk = ser.read(256)
                if chunk:
                    buf += chunk
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        self.on_line(line.decode("utf-8", errors="replace").rstrip("\r"))
        finally:
            ser.close()
