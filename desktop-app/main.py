"""
main.py - ZxheiFi Setup Companion.

Three tabs: Flash Firmware (wraps esptool + a serial boot-log monitor),
Configure MikroTik (checkbox-driven RouterOS API execution, reusing
mikrotik_client.py/mikrotik_commands.py), and Guide (a static in-app
walkthrough, see guide_content.py).
"""
import sys

# esptool worker mode (see flasher.ESPTOOL_WORKER_ARG) is handled before
# anything else is imported: loading customtkinter/PIL/the tabs first cost
# ~9s per esptool run in the installed app and ~29s in the portable one -
# on EVERY detect and flash.
if __name__ == "__main__" and len(sys.argv) > 1 and sys.argv[1] == "--esptool-worker":
    import esptool
    sys.argv = ["esptool"] + sys.argv[2:]
    try:
        esptool._main()  # esptool's own CLI entry: clean error messages, same output FlashJob/DeviceProbe parse
    except SystemExit as exc:
        sys.exit(exc.code if isinstance(exc.code, int) else 1)
    sys.exit(0)

import json
import os
import secrets
import threading
import tkinter as tk
import tkinter.filedialog as fd
import tkinter.messagebox as mbox

import customtkinter as ctk

import theme
import flasher
import configurator
import guide_content
import gui_upload
import netcheck
import mikrotik_commands as cmds
from mikrotik_client import MikrotikClient

ASSETS_DIR = os.path.join(os.path.dirname(__file__), "assets")
# Packaged builds carry their own copy of the firmware (build.py bundles it
# under firmware/). The old "../firmware" path pointed outside PyInstaller's
# unpack folder, so the exes never had a default .bin. Running from source
# still uses the project's firmware folder.
_BUNDLED_FIRMWARE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "firmware", "zxheifi_firmware.bin")
DEFAULT_FIRMWARE = _BUNDLED_FIRMWARE if os.path.isfile(_BUNDLED_FIRMWARE) else os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "firmware", "zxheifi_firmware.bin")
)
# v2: the Sub Vendo firmware ships the same way (build.py bundles
# subvendo/zxheifi_subvendo.bin under firmware/).
_BUNDLED_SUB_FIRMWARE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "firmware", "zxheifi_subvendo.bin")
DEFAULT_SUB_FIRMWARE = _BUNDLED_SUB_FIRMWARE if os.path.isfile(_BUNDLED_SUB_FIRMWARE) else os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "subvendo", "zxheifi_subvendo.bin")
)
_BUNDLED_CHARGING_FIRMWARE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "firmware",
                                          "zxheifi_charging.bin")
DEFAULT_CHARGING_FIRMWARE = _BUNDLED_CHARGING_FIRMWARE if os.path.isfile(_BUNDLED_CHARGING_FIRMWARE) else \
    os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "charging", "zxheifi_charging.bin"))
DEVICE_MAIN = "Main unit"
DEVICE_SUB = "Sub Vendo"
DEVICE_CHARGING = "Charging Station"
DEVICE_FIRMWARE = {DEVICE_MAIN: DEFAULT_FIRMWARE, DEVICE_SUB: DEFAULT_SUB_FIRMWARE,
                   DEVICE_CHARGING: DEFAULT_CHARGING_FIRMWARE}
# Network Settings (incl. the generated NodeMCU API password) persist
# between runs - re-running Config with a freshly generated password would
# silently break a NodeMCU already set up with the old one. The router's
# own admin password is deliberately never saved.
# Same idea for the customer GUI (build.py bundles mikrotik/gui as gui/),
# used by Configure MikroTik's Upload GUI Files button.
_BUNDLED_GUI = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gui")
GUI_DIR = _BUNDLED_GUI if os.path.isfile(os.path.join(_BUNDLED_GUI, "login.html")) else os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mikrotik", "gui")
)
APP_VERSION = "2.0.0-dev"   # keep in step with firmware FIRMWARE_VERSION and the GUI's ZX_GUI_VERSION
SETTINGS_FILE = os.path.join(os.environ.get("APPDATA", os.path.expanduser("~")),
                             "ZxheiFi", "setup-companion.json")


def load_settings():
    try:
        with open(SETTINGS_FILE, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def save_settings(data):
    """Merges `data` into the settings file, so tabs saving different keys
    (Configure MikroTik's router details, the Guide's language) never wipe
    each other's."""
    merged = load_settings()
    merged.update(data)
    try:
        os.makedirs(os.path.dirname(SETTINGS_FILE), exist_ok=True)
        with open(SETTINGS_FILE, "w", encoding="utf-8") as f:
            json.dump(merged, f, indent=2)
    except OSError:
        pass


class FlashTab(ctk.CTkFrame):
    def __init__(self, master, on_device_detected=None):
        super().__init__(master, fg_color="transparent")
        self._monitor = None
        self._busy = False  # true while a flash holds the port
        self._flash_erased = False  # whether the last flash wiped the whole chip
        self._probe = None  # the running flasher.DeviceProbe, if any
        # Called with (mac, ip) once a port's device has been probed - lets
        # App wire the detected NodeMCU MAC into the Configure MikroTik
        # tab's "NodeMCU MAC" field instead of the operator typing it in by
        # hand (a real source of copy-paste errors for the static DHCP
        # lease/ip-binding that field feeds into).
        self.on_device_detected = on_device_detected
        # Scrollable so the log/progress area at the bottom stays reachable
        # even when the window is shorter than the tab's content.
        self.scroll = ctk.CTkScrollableFrame(self, fg_color="transparent")
        self.scroll.pack(fill="both", expand=True)
        self._build()
        self._refresh_ports()

    def _build(self):
        parent = self.scroll
        row = ctk.CTkFrame(parent, fg_color="transparent")
        row.pack(fill="x", padx=16, pady=(16, 8))

        ctk.CTkLabel(row, text="COM Port:").grid(row=0, column=0, sticky="w", padx=(0, 8))
        # The menu shows "COM5 - USB-SERIAL CH340"-style labels (USB first,
        # Bluetooth ports marked) so the NodeMCU's port is obvious;
        # _port_devices maps each label back to its COM device.
        self.port_var = tk.StringVar()
        self._port_devices = {}
        self.port_menu = ctk.CTkOptionMenu(row, variable=self.port_var, values=[], width=320,
                                            command=self._on_port_change)
        self.port_menu.grid(row=0, column=1, sticky="w")
        ctk.CTkButton(row, text="Refresh", width=80, command=self._refresh_ports,
                      **theme.SECONDARY_BUTTON_KW).grid(row=0, column=2, padx=8)
        self.scan_btn = ctk.CTkButton(row, text="Scan Device", width=120, command=self._toggle_scan,
                                      **theme.BUTTON_KW)
        self.scan_btn.grid(row=0, column=3)

        # Read on demand with Scan Device (works even on a device with no
        # WiFi configured yet - see flasher.DeviceProbe), and also picked up
        # automatically from a flash's own output.
        device_row = ctk.CTkFrame(parent, fg_color="transparent")
        device_row.pack(fill="x", padx=16, pady=(0, 8))
        ctk.CTkLabel(device_row, text="Detected Device:").grid(row=0, column=0, sticky="w", padx=(0, 8))
        self.device_status_var = tk.StringVar(value="(select a port)")
        ctk.CTkLabel(device_row, textvariable=self.device_status_var,
                     text_color=theme.TEXT_DIM).grid(row=0, column=1, sticky="w")

        # v2: which firmware this board gets. A Sub Vendo is an extra coin
        # box - its MAC must not land in Configure MikroTik's NodeMCU MAC.
        type_row = ctk.CTkFrame(parent, fg_color="transparent")
        type_row.pack(fill="x", padx=16, pady=(8, 0))
        ctk.CTkLabel(type_row, text="Device type:").grid(row=0, column=0, sticky="w", padx=(0, 8))
        self.device_type = ctk.CTkSegmentedButton(type_row, values=[DEVICE_MAIN, DEVICE_SUB, DEVICE_CHARGING],
                                                  command=self._on_device_type,
                                                  selected_color=theme.ACCENT,
                                                  selected_hover_color=theme.ACCENT_DARK)
        self.device_type.set(DEVICE_MAIN)
        self.device_type.grid(row=0, column=1, sticky="w")
        self.device_hint_var = tk.StringVar(value=self._device_hint(DEVICE_MAIN))
        ctk.CTkLabel(type_row, textvariable=self.device_hint_var, text_color=theme.TEXT_DIM,
                     justify="left", anchor="w").grid(row=1, column=0, columnspan=2, sticky="w", pady=(4, 0))

        fw_row = ctk.CTkFrame(parent, fg_color="transparent")
        fw_row.pack(fill="x", padx=16, pady=8)
        ctk.CTkLabel(fw_row, text="Firmware .bin:").grid(row=0, column=0, sticky="w", padx=(0, 8))
        self.fw_var = tk.StringVar(value=DEFAULT_FIRMWARE)
        ctk.CTkEntry(fw_row, textvariable=self.fw_var, width=380).grid(row=0, column=1, sticky="w")
        ctk.CTkButton(fw_row, text="Browse...", width=90, command=self._browse_fw,
                      **theme.SECONDARY_BUTTON_KW).grid(row=0, column=2, padx=8)

        action_row = ctk.CTkFrame(parent, fg_color="transparent")
        action_row.pack(fill="x", padx=16, pady=8)
        self.flash_btn = ctk.CTkButton(action_row, text="Flash Firmware", command=self._start_flash,
                                        **theme.BUTTON_KW)
        self.flash_btn.pack(side="left")
        self.monitor_btn = ctk.CTkButton(action_row, text="Start Serial Monitor",
                                          command=self._toggle_monitor, **theme.SECONDARY_BUTTON_KW)
        self.monitor_btn.pack(side="left", padx=8)

        # Off by default: a normal flash keeps the device's saved setup,
        # accounts and vouchers (see flasher.FlashJob's erase_all).
        erase_row = ctk.CTkFrame(parent, fg_color="transparent")
        erase_row.pack(fill="x", padx=16, pady=(0, 4))
        self.erase_var = tk.BooleanVar(value=False)
        ctk.CTkCheckBox(erase_row, text="Erase everything first (fresh start)", variable=self.erase_var,
                        fg_color=theme.ACCENT).pack(side="left")
        ctk.CTkLabel(erase_row, text="Deletes saved WiFi/MikroTik setup, admin accounts, vouchers, "
                                     "subscribers and logs on the NodeMCU.",
                     text_color=theme.TEXT_DIM).pack(side="left", padx=8)

        self.progress = ctk.CTkProgressBar(parent, progress_color=theme.ACCENT)
        self.progress.set(0)
        self.progress.pack(fill="x", padx=16, pady=(8, 0))

        self.log = ctk.CTkTextbox(parent, fg_color=theme.PANEL_INSET, text_color=theme.TEXT, height=220)
        self.log.pack(fill="both", expand=True, padx=16, pady=16)

    def _log(self, text):
        self.after(0, lambda: (self.log.insert("end", text + "\n"), self.log.see("end")))

    def _selected_port(self):
        return self._port_devices.get(self.port_var.get(), "")

    def _refresh_ports(self):
        self._cancel_probe()
        ports = flasher.list_com_ports()   # [(device, label)], USB ports first
        self._port_devices = {label: device for device, label in ports}
        self.port_menu.configure(values=[label for _, label in ports] or ["(none found)"])
        if not ports:
            self.port_var.set("(none found)")
            self.device_status_var.set("(no ports found - plug in the NodeMCU, then Refresh)")
            return
        self.port_var.set(ports[0][1])
        self._show_scan_hint()

    def _show_scan_hint(self):
        if "Bluetooth" in self.port_var.get():
            self.device_status_var.set("That's a Bluetooth port - pick the USB one (e.g. CH340/CP210x)")
        else:
            self.device_status_var.set("Click Scan Device to read the NodeMCU's MAC/IP (optional)")

    def _on_port_change(self, _label):
        # No automatic scan: Bluetooth ports in the list can take up to a
        # minute to give up. Scanning is the Scan Device button's job.
        self._cancel_probe()
        self._show_scan_hint()

    def _toggle_scan(self):
        if self._probe:
            self._cancel_probe()
            self._show_scan_hint()
        else:
            self._detect_device(self._selected_port())

    def _cancel_probe(self):
        if self._probe:
            self._probe.cancel()
            self._probe = None
        self.scan_btn.configure(text="Scan Device")

    def _detect_device(self, port):
        if not port:
            mbox.showerror("No port selected", "Select the NodeMCU's COM port first.")
            return
        if self._busy:
            return
        self._cancel_probe()
        self.scan_btn.configure(text="Cancel Scan")
        self.device_status_var.set(f"Scanning {port}... (10-30s)")

        def on_status(text):
            self.after(0, lambda: self._probe is probe and self.device_status_var.set(text))

        def on_result(mac, ip):
            def finish():
                if self._probe is not probe:
                    return  # cancelled, or superseded by a newer scan
                self._probe = None
                self.scan_btn.configure(text="Scan Device")
                if mac:
                    self._report_mac(mac, ip)
            self.after(0, finish)

        probe = flasher.DeviceProbe(port, on_status=on_status, on_result=on_result)
        self._probe = probe
        probe.start()

    @staticmethod
    def _device_hint(kind):
        if kind == DEVICE_SUB:
            return ("Sub Vendo = an extra coin box for the same WiFi. After flashing it opens the "
                    "\"ZxheiFi-Sub-Setup\" WiFi:\nenter the WiFi of its spot and the pairing code "
                    "from Admin > Vendos > Add Vendo.")
        if kind == DEVICE_CHARGING:
            return ("Charging Station = a coin-op phone charger (4 ports, buttons + screen). After flashing it opens "
                    "the \"ZxheiFi-Charge-Setup\" WiFi:\nenter the WiFi of its spot and the pairing code from "
                    "Admin > Vendos > Add Vendo (type: Charging Station).")
        return "Main unit = the NodeMCU that runs the shop (talks to the MikroTik)."

    def _is_sub(self):
        """Any box that isn't the main unit (Sub Vendo or Charging Station)."""
        return self.device_type.get() != DEVICE_MAIN

    def _on_device_type(self, kind):
        # Swap the default .bin only if the field still holds a default -
        # a file the operator picked with Browse... is kept.
        if self.fw_var.get() in DEVICE_FIRMWARE.values():
            self.fw_var.set(DEVICE_FIRMWARE.get(kind, DEFAULT_FIRMWARE))
        self.device_hint_var.set(self._device_hint(kind))

    def _report_mac(self, mac, ip):
        """Hands a detected MAC to Configure MikroTik - main units only."""
        if self._is_sub():
            self._log(f"{self.device_type.get()} MAC {mac} (not copied to Configure MikroTik - "
                      "boxes need no router setup)")
            return
        if self.on_device_detected:
            self.on_device_detected(mac, ip)

    def _browse_fw(self):
        path = fd.askopenfilename(title="Select firmware .bin", filetypes=[("Firmware binary", "*.bin")])
        if path:
            self.fw_var.set(path)

    def _start_flash(self):
        port = self._selected_port()
        fw = self.fw_var.get()
        if not port:
            mbox.showerror("No port selected", "Select a COM port first.")
            return
        if self._busy:
            mbox.showerror("Busy", "A flash is already running.")
            return
        if not os.path.isfile(fw):
            mbox.showerror("Firmware not found", f"No such file:\n{fw}")
            return
        erase_all = self.erase_var.get()
        if erase_all and not mbox.askyesno(
                "Erase everything?",
                "This wipes the NodeMCU completely before flashing:\n\n"
                "• WiFi / MikroTik setup (Setup Wizard)\n"
                "• Admin accounts and passwords\n"
                "• Vouchers, subscribers, rate profiles, settings\n"
                "• Activity logs and sales history\n\n"
                "It will start in Setup Mode (\"ZxheiFi-Setup\" WiFi) like a new unit.\n\n"
                "Continue?", icon="warning"):
            return
        if self._monitor:
            self._toggle_monitor()  # stop it, can't share the port with esptool
        self._cancel_probe()  # flashing reads the chip anyway - no need to wait for detection
        self._busy = True
        self.flash_btn.configure(state="disabled")
        self.monitor_btn.configure(state="disabled")
        self.progress.set(0)
        self._flash_erased = erase_all
        self._log(f"=== Flashing {os.path.basename(fw)} to {port}"
                  + (" (erasing everything first - this takes ~15s longer)" if erase_all else "") + " ===")

        job = flasher.FlashJob(
            port, fw,
            on_line=self._log,
            on_progress=lambda pct: self.after(0, lambda: self.progress.set(pct / 100)),
            on_done=self._on_flash_done,
            erase_all=erase_all,
            on_mac=lambda mac: self.after(0, lambda: self._on_flash_mac(mac)),
        )
        job.start()

    def _on_flash_mac(self, mac):
        self.device_status_var.set(f"MAC {mac}  (read during flash)")
        self._report_mac(mac, None)

    def _on_flash_done(self, ok, code):
        def finish():
            self._busy = False
            self.flash_btn.configure(state="normal")
            self.monitor_btn.configure(state="normal")
            self.progress.set(1 if ok else 0)
            self._log("=== Flash succeeded ===" if ok else f"=== Flash failed (exit {code}) ===")
            if ok and self._is_sub():
                charging = self.device_type.get() == DEVICE_CHARGING
                self._log(f"{self.device_type.get()} flashed. It opens the "
                          f"\"{'ZxheiFi-Charge-Setup' if charging else 'ZxheiFi-Sub-Setup'}\" WiFi (first boot, "
                          "or press FLASH while the blue LED blinks fast after power-on). Get the pairing code "
                          "from Admin > Vendos > Add Vendo"
                          + (" (type: Charging Station)." if charging else "."))
            elif ok and self._flash_erased:
                self._log("Fresh start: the NodeMCU formats its storage on this first boot (a few "
                          "seconds), then opens the \"ZxheiFi-Setup\" WiFi for the Setup Wizard.")
                self.erase_var.set(False)  # one-shot, so a later re-flash doesn't wipe it again
        self.after(0, finish)

    def _toggle_monitor(self):
        if self._busy and not self._monitor:
            mbox.showerror("Busy", "Wait for the flash to finish first.")
            return
        if self._monitor:
            self._monitor.stop()
            self._monitor = None
            self.monitor_btn.configure(text="Start Serial Monitor")
            self._log("--- serial monitor stopped ---")
            return
        port = self._selected_port()
        if not port:
            mbox.showerror("No port selected", "Select a COM port first.")
            return
        self._cancel_probe()  # frees the port
        self._log(f"--- serial monitor started on {port} (resetting board) ---")
        self._monitor = flasher.SerialMonitor(port, on_line=self._log)
        self._monitor.start()
        self.monitor_btn.configure(text="Stop Serial Monitor")


class MikrotikTab(ctk.CTkFrame):
    ROLES = ["wan", "hotspot", "pppoe", "lan", "unused"]

    def __init__(self, master):
        super().__init__(master, fg_color="transparent")
        self.check_vars = {}
        self.var_entries = {}
        self.router_info = None       # filled by Test Connection (configurator.probe_router)
        self._busy = False
        self.settings = load_settings()
        # Scrollable so Network Settings/Features/Port Roles/log all stay
        # reachable regardless of window height.
        self.scroll = ctk.CTkScrollableFrame(self, fg_color="transparent")
        self.scroll.pack(fill="both", expand=True)
        self._build()

    # ---- layout ---------------------------------------------------------

    def _build(self):
        parent = self.scroll
        top = ctk.CTkFrame(parent, fg_color="transparent")
        top.pack(fill="x", padx=16, pady=(16, 8))

        ctk.CTkLabel(top, text="Device:").grid(row=0, column=0, sticky="w", padx=(0, 8))
        self.device_var = tk.StringVar(value=self.settings.get("device", "hap_lite"))
        ctk.CTkRadioButton(top, text="hAP lite", variable=self.device_var, value="hap_lite",
                            command=self._on_device_change, fg_color=theme.ACCENT).grid(row=0, column=1, padx=4)
        ctk.CTkRadioButton(top, text="hEX", variable=self.device_var, value="hex",
                            command=self._on_device_change, fg_color=theme.ACCENT).grid(row=0, column=2, padx=4)

        conn = ctk.CTkFrame(parent, fg_color="transparent")
        conn.pack(fill="x", padx=16, pady=8)
        ctk.CTkLabel(conn, text="Router Host:").grid(row=0, column=0, sticky="w")
        self.host_var = tk.StringVar(value=self.settings.get("host", "192.168.88.1"))
        ctk.CTkEntry(conn, textvariable=self.host_var, width=140).grid(row=0, column=1, padx=8)
        ctk.CTkLabel(conn, text="User:").grid(row=0, column=2, sticky="w")
        self.user_var = tk.StringVar(value="admin")
        ctk.CTkEntry(conn, textvariable=self.user_var, width=100).grid(row=0, column=3, padx=8)
        ctk.CTkLabel(conn, text="Password:").grid(row=0, column=4, sticky="w")
        self.pass_var = tk.StringVar()
        ctk.CTkEntry(conn, textvariable=self.pass_var, show="*", width=140).grid(row=0, column=5, padx=8)
        self.test_btn = ctk.CTkButton(conn, text="Test Connection", command=self._test_connection,
                                       **theme.SECONDARY_BUTTON_KW)
        self.test_btn.grid(row=0, column=6, padx=8)
        self.netcheck_btn = ctk.CTkButton(conn, text="Network Check", command=self._start_netcheck,
                                          width=120, **theme.SECONDARY_BUTTON_KW)
        self.netcheck_btn.grid(row=0, column=7)
        self.conn_status = ctk.CTkLabel(conn, text="Not connected", text_color=theme.TEXT_DIM,
                                        wraplength=420, justify="left")
        self.conn_status.grid(row=1, column=0, columnspan=8, sticky="w", pady=(6, 0))

        body = ctk.CTkFrame(parent, fg_color="transparent")
        body.pack(fill="both", expand=True, padx=16, pady=8)
        body.grid_columnconfigure(0, weight=1)
        body.grid_columnconfigure(1, weight=1)

        var_frame = ctk.CTkFrame(body, fg_color=theme.PANEL)
        var_frame.grid(row=0, column=0, sticky="nsew", padx=(0, 8), pady=(0, 8))
        ctk.CTkLabel(var_frame, text="Network Settings", font=("", 14, "bold")).pack(anchor="w", padx=12, pady=(10, 4))
        self._var_fields(var_frame)

        feat_frame = ctk.CTkFrame(body, fg_color=theme.PANEL)
        feat_frame.grid(row=0, column=1, sticky="nsew", padx=(8, 0), pady=(0, 8))
        ctk.CTkLabel(feat_frame, text="Features to Configure", font=("", 14, "bold")).pack(anchor="w", padx=12, pady=(10, 4))
        self._feature_checklist(feat_frame)

        port_frame = ctk.CTkFrame(body, fg_color=theme.PANEL)
        port_frame.grid(row=1, column=0, columnspan=2, sticky="nsew")
        ctk.CTkLabel(port_frame, text="Port Roles", font=("", 14, "bold")).pack(anchor="w", padx=12, pady=(10, 4))
        ctk.CTkLabel(port_frame, text="What each physical port is used for. After Test Connection this "
                     "shows the router's real ports. Default: ether1 = WAN (internet in), everything else "
                     "= Hotspot. Use \"pppoe\" for a dedicated PPPoE port, \"lan\" for a plain port with "
                     "no login page (e.g. the shop owner's own PC).",
                     text_color=theme.TEXT_DIM, wraplength=700, justify="left", font=("", 11)).pack(anchor="w", padx=12, pady=(0, 6))
        self.port_role_rows_frame = ctk.CTkFrame(port_frame, fg_color="transparent")
        self.port_role_rows_frame.pack(fill="x", padx=12, pady=(0, 10))
        self.port_role_vars = {}
        self._build_port_role_rows()

        bottom = ctk.CTkFrame(parent, fg_color="transparent")
        bottom.pack(fill="x", padx=16, pady=(8, 0))
        self.config_btn = ctk.CTkButton(bottom, text="Config", command=self._start_execute,
                                         state="disabled", **theme.BUTTON_KW)
        self.config_btn.pack(side="left")
        self.upload_btn = ctk.CTkButton(bottom, text="Upload GUI Files", command=self._start_upload,
                                         state="disabled", **theme.SECONDARY_BUTTON_KW)
        self.upload_btn.pack(side="left", padx=8)
        self.remove_btn = ctk.CTkButton(bottom, text="Remove ZxheiFi Config", command=self._start_remove,
                                         state="disabled", **theme.DANGER_BUTTON_KW)
        self.remove_btn.pack(side="left", padx=8)

        self.log = ctk.CTkTextbox(parent, fg_color=theme.PANEL_INSET, text_color=theme.TEXT, height=220)
        self.log.pack(fill="both", expand=True, padx=16, pady=16)

    def apply_detected_device(self, mac, ip):
        # Fed by FlashTab's DeviceProbe: the real NodeMCU MAC goes straight
        # into the static DHCP lease/hotspot bypass instead of being
        # hand-typed. nodemcuIP is the STATIC address to assign, so the
        # device's current IP (if any) is deliberately not copied.
        if mac and "nodemcuMAC" in self.var_entries:
            self.var_entries["nodemcuMAC"].set(mac)

    def _var_fields(self, parent):
        saved = self.settings.get("vars", {})
        device = self.device_var.get()
        fields = [
            ("ssid", "WiFi SSID", cmds.DEFAULT_VARS["ssid"]),
            ("wpaPassword", "WiFi Password", cmds.DEFAULT_VARS["wpaPassword"]),
            ("nodemcuIP", "NodeMCU IP", cmds.DEFAULT_VARS["nodemcuIP"]),
            ("nodemcuMAC", "NodeMCU MAC", cmds.DEFAULT_VARS["nodemcuMAC"]),
            ("apiPassword", "NodeMCU API pass", secrets.token_urlsafe(9)),
            ("bandwidthDown", "Bandwidth Down", cmds.DEVICE_DEFAULTS[device]["bandwidthDown"]),
            ("bandwidthUp", "Bandwidth Up", cmds.DEVICE_DEFAULTS[device]["bandwidthUp"]),
        ]
        for key, label, default in fields:
            row = ctk.CTkFrame(parent, fg_color="transparent")
            row.pack(fill="x", padx=12, pady=3)
            ctk.CTkLabel(row, text=label, width=110, anchor="w").pack(side="left")
            var = tk.StringVar(value=saved.get(key, default))
            ctk.CTkEntry(row, textvariable=var).pack(side="left", fill="x", expand=True)
            self.var_entries[key] = var
        ctk.CTkLabel(parent, text="WiFi Password: leave BLANK for an open WiFi like every piso-WiFi "
                     "(customers pay on the login page). "
                     "Bandwidth = ~90% of your real internet speed (used by Gaming QoS). "
                     "NodeMCU API pass is generated once and remembered - you'll type it into the "
                     "NodeMCU's Setup Wizard.", text_color=theme.TEXT_DIM, wraplength=340,
                     justify="left", font=("", 11)).pack(anchor="w", padx=12, pady=(4, 10))

    def _feature_checklist(self, parent):
        self.feature_rows = {}
        saved = self.settings.get("features", {})
        for feat in cmds.FEATURES:
            row = ctk.CTkFrame(parent, fg_color="transparent")
            row.pack(fill="x", padx=12, pady=3)
            var = tk.BooleanVar(value=feat["required"] or saved.get(feat["key"], False))
            cb = ctk.CTkCheckBox(row, text=feat["label"], variable=var, fg_color=theme.ACCENT,
                                  hover_color=theme.ACCENT_DARK)
            if feat["required"]:
                cb.configure(state="disabled")
            cb.pack(anchor="w")
            ctk.CTkLabel(row, text=feat["description"], text_color=theme.TEXT_DIM,
                         wraplength=340, justify="left", font=("", 11)).pack(anchor="w", padx=24)
            self.check_vars[feat["key"]] = var
            self.feature_rows[feat["key"]] = row
        self._show_device_features()

    def _show_device_features(self):
        device = self.device_var.get()
        for feat in cmds.FEATURES:
            if feat["hap_lite_only"]:
                row = self.feature_rows[feat["key"]]
                if device == "hap_lite":
                    row.pack(fill="x", padx=12, pady=3)
                else:
                    row.pack_forget()

    def _on_device_change(self):
        self._show_device_features()
        defaults = cmds.DEVICE_DEFAULTS[self.device_var.get()]
        self.var_entries["bandwidthDown"].set(defaults["bandwidthDown"])
        self.var_entries["bandwidthUp"].set(defaults["bandwidthUp"])
        self._build_port_role_rows()

    def _build_port_role_rows(self):
        for child in self.port_role_rows_frame.winfo_children():
            child.destroy()
        device = self.device_var.get()
        # The connected router's real ports - but only while it IS the
        # selected device. Switching the toggle to hEX after testing a hAP
        # lite kept showing the hAP lite's 4 ports + wlan1.
        info = self.router_info or {}
        if info.get("ports") and configurator.board_matches(device, info.get("board", "")):
            ports = info["ports"]
        else:
            ports = cmds.DEVICE_PORTS[device]
        defaults = cmds.default_port_roles(device, ports)
        saved = self.settings.get("port_roles", {})
        self.port_role_vars = {}
        for i, port in enumerate(ports):
            cell = ctk.CTkFrame(self.port_role_rows_frame, fg_color="transparent")
            cell.grid(row=i // 3, column=i % 3, padx=8, pady=4, sticky="w")
            ctk.CTkLabel(cell, text=port, width=60, anchor="w").pack(side="left")
            var = tk.StringVar(value=saved.get(port, defaults[port]))
            ctk.CTkOptionMenu(cell, variable=var, values=self.ROLES, width=110).pack(side="left")
            self.port_role_vars[port] = var

    def _collect_port_roles(self):
        return {port: var.get() for port, var in self.port_role_vars.items() if var.get() != "unused"}

    def _collect_vars(self):
        v = dict(cmds.DEFAULT_VARS)
        for key, var in self.var_entries.items():
            v[key] = var.get().strip()
        return v

    def _log(self, text):
        self.after(0, lambda: (self.log.insert("end", text + "\n"), self.log.see("end")))

    def _set_busy(self, busy):
        self._busy = busy
        state = "disabled" if busy else "normal"
        self.test_btn.configure(state=state)
        ready = "normal" if (not busy and self.router_info) else "disabled"
        self.config_btn.configure(state=ready)
        self.upload_btn.configure(state=ready)
        self.remove_btn.configure(state=ready)

    # ---- Test Connection -------------------------------------------------

    def _test_connection(self):
        host, user, password = self.host_var.get().strip(), self.user_var.get(), self.pass_var.get()
        self.router_info = None
        self._set_busy(True)
        self.conn_status.configure(text="Connecting...", text_color=theme.TEXT_DIM)

        def worker():
            # Whatever goes wrong, the UI must hear back - a narrower
            # except leaves "Connecting..." stuck forever.
            try:
                client = MikrotikClient(host)
                client.connect(user, password)
                info = configurator.probe_router(client, host)
                client.close()
                self.after(0, lambda: self._on_probe(info))
            except Exception as exc:
                msg = f"{type(exc).__name__}: {exc}"
                self.after(0, lambda: self._on_probe_failed(msg))

        threading.Thread(target=worker, daemon=True).start()

    def _on_probe_failed(self, message):
        self.conn_status.configure(text=message, text_color=theme.ALERT)
        self._log(f"=== Test Connection failed: {message}")
        hint = netcheck.wrong_path_hint(self.host_var.get().strip())
        if hint:
            self._log(f"    [!] {hint}")
        if "invalid user name or password" in message.lower():
            self._log("    If the password IS right, you are probably reaching a different router with the same")
            self._log("    address (e.g. your main router is also 192.168.88.1). Click 'Network Check'.")
        elif "10054" in message or "reset" in message.lower() or "timed out" in message.lower():
            self._log("    The connection dropped. Click 'Network Check' - it tests the cable, the path and the router.")
        self._set_busy(False)

    def _start_netcheck(self):
        host = self.host_var.get().strip()
        self.netcheck_btn.configure(state="disabled")

        def worker():
            try:
                netcheck.run_check(host, self._log)
            except Exception as exc:
                self._log(f"[Network Check error] {type(exc).__name__}: {exc}")
            finally:
                self.after(0, lambda: self.netcheck_btn.configure(state="normal"))

        threading.Thread(target=worker, daemon=True).start()

    def _on_probe(self, info):
        self.router_info = info
        detected = configurator.detect_device(info["board"])
        if detected and detected != self.device_var.get():
            self.device_var.set(detected)
            self._on_device_change()
        else:
            self._build_port_role_rows()

        text = (f"Connected to \"{info['identity']}\" - {info['board']}, RouterOS {info['version']} "
                f"({info['arch']})")
        color = theme.PESO
        if not detected:
            text += "  |  Unsupported board - double-check this is the right router."
            color = theme.ALERT
        if info["foreign"]:
            text += "  |  WARNING: this router already has its own config (see log)."
            color = theme.ALERT
        self.conn_status.configure(text=text, text_color=color)

        self._log(f"=== Test Connection: {text}")
        self._log(f"    Ports: {', '.join(info['ports']) or '(none reported)'}")
        if info.get("mgmt_interface"):
            self._log(f"    This PC reaches it through: {info['mgmt_interface']} ({info.get('mgmt_address')})")
        if info["foreign"]:
            summary = ", ".join(f"{n} {label}" for label, n in info["foreign"].items())
            self._log(f"    Existing config NOT made by ZxheiFi: {summary}.")
            self._log("    If this is a router your home/customers already use, STOP - you may be")
            self._log("    connected to the wrong router. For a new unit, reset it with")
            self._log("    'No Default Configuration' first (see the Guide tab).")
        if not info.get("pc_direct", True):
            self._log(f"    [!] This PC ({info.get('pc_ip')}) is not on any of this router's own networks - you're")
            self._log("        reaching it THROUGH another router. Config/Upload are blocked. Plug the PC straight")
            self._log("        into the router you're setting up (and see Network Check).")
        blocked = configurator.device_mode_blocks(info, self._checked_feature_keys())
        if blocked:
            self._log(f"    RouterOS device-mode forbids: {', '.join(blocked)}. Fix it before Config:")
            self._log(f"      1. Winbox > New Terminal:  {configurator.device_mode_command(blocked)}")
            self._log("      2. Within 5 minutes, unplug the router's power for 5 seconds and plug it back")
            self._log("         (or tap its reset button once - don't hold it).")
            self._log("      3. Test Connection again.")
        if not info["has_hotspot_pkg"]:
            self._log("    The 'hotspot' package is not installed - Config will refuse to run.")
            self._log("    Winbox > System > Packages > Check For Updates > select 'hotspot' > Enable >")
            self._log("    Apply Changes (router needs internet on ether1), or upload the .npk manually.")
        self._set_busy(False)

    # ---- Config -----------------------------------------------------------

    def _preflight(self, device, v):
        info = self.router_info
        if not configurator.board_matches(device, info["board"]):
            mbox.showerror("Wrong router?",
                           f"You selected {device}, but the router at {self.host_var.get()} reports board "
                           f"\"{info['board']}\".\n\nConfig was NOT run. Make sure your PC is plugged "
                           "straight into the router you mean to set up (and not reaching another router "
                           "through WiFi), then Test Connection again.")
            return False
        if not info.get("pc_direct", True):
            mbox.showerror("Not a direct connection",
                           f"This PC ({info.get('pc_ip')}) isn't on any of \"{info['identity']}\"'s own networks, "
                           "so it's reaching that router through another one.\n\nConfig was NOT run. Plug the PC "
                           "straight into the router you're setting up, then Test Connection again "
                           "(Network Check shows the path).")
            return False
        if not info["has_hotspot_pkg"]:
            mbox.showerror("Hotspot package missing",
                           "This router has no 'hotspot' package, so the login page can't work.\n\n"
                           "Install it first (see the log below Test Connection), then Test Connection again.")
            return False
        blocked = configurator.device_mode_blocks(info, self._checked_feature_keys())
        fix = (f"\n\n1. Winbox > New Terminal:\n   {configurator.device_mode_command(blocked)}\n"
               "2. Within 5 minutes unplug the router's power for 5 seconds, then plug it back "
               "(or tap the reset button once - don't hold it).\n3. Test Connection again.")
        if "hotspot" in blocked:
            mbox.showerror("Hotspot blocked by device-mode",
                           "RouterOS device-mode on this router doesn't allow the hotspot - Config would "
                           "finish but the login page would never appear." + fix)
            return False
        if "scheduler" in blocked and not mbox.askyesno(
                "Scheduler blocked by device-mode",
                "Daily Reboot / Random MAC Fix need the scheduler, which device-mode doesn't allow - "
                "those steps would fail." + fix + "\n\nContinue without them for now?", default="no"):
            return False
        if info["foreign"]:
            summary = "\n".join(f"  - {n} {label}" for label, n in info["foreign"].items())
            if not mbox.askyesno("This router is already configured",
                                 f"\"{info['identity']}\" ({info['board']}) already has config that "
                                 f"ZxheiFi didn't create:\n{summary}\n\nIf this is a router your household "
                                 "or customers use, press No - Config can take them offline.\n\n"
                                 "Continue anyway?", icon="warning", default="no"):
                return False
        if v["nodemcuMAC"].lower() in ("", cmds.DEFAULT_VARS["nodemcuMAC"]):
            if not mbox.askyesno("NodeMCU MAC not set",
                                 "The NodeMCU MAC is still the placeholder. Plug the NodeMCU into this PC "
                                 "and pick its COM port in the Flash Firmware tab to fill it in "
                                 "automatically.\n\nContinue without it?", default="no"):
                return False
        if v["wpaPassword"] and len(v["wpaPassword"]) < 8:
            mbox.showerror("WiFi password", "A WiFi password needs at least 8 characters - or leave it "
                           "blank for an open WiFi (piso-WiFi style).")
            return False
        if len(v["apiPassword"]) < 8:
            mbox.showerror("NodeMCU API password", "Use at least 8 characters for the NodeMCU API password.")
            return False
        roles = self._collect_port_roles()
        if not [p for p, r in roles.items() if r == "hotspot"]:
            mbox.showerror("Port Roles", "At least one port (or wlan1) must have the \"hotspot\" role.")
            return False
        if not [p for p, r in roles.items() if r == "wan"]:
            mbox.showerror("Port Roles", "One port must be \"wan\" - the one your internet cable goes into.")
            return False
        return True

    def _checked_feature_keys(self):
        return [key for key, var in self.check_vars.items() if var.get()]

    def _start_execute(self):
        device = self.device_var.get()
        v = self._collect_vars()
        if not self._preflight(device, v):
            return
        host, user, password = self.host_var.get().strip(), self.user_var.get(), self.pass_var.get()
        port_roles = self._collect_port_roles()
        checked = [f for f in cmds.FEATURES if self.check_vars[f["key"]].get()
                   and not (f["hap_lite_only"] and device != "hap_lite")]
        expected_board = self.router_info["board"]
        self._set_busy(True)
        self._log(f"=== Configuring {len(checked)} feature(s) on {host} ({expected_board}) ===")

        def worker():
            client = MikrotikClient(host)
            try:
                client.connect(user, password)
                # Re-probe: the plan depends on which port carries this
                # session right now, and the router must still be the same one.
                router = configurator.probe_router(client, host)
                if router["board"] != expected_board:
                    self._log(f"[STOPPED] The router changed since Test Connection "
                              f"({expected_board} -> {router['board']}). Nothing was changed.")
                    return
                plan = [(f["label"], f["build"](device, v, port_roles, router)) for f in checked]
                counts = configurator.execute(client, plan, self._log)
                self._log(f"=== Done: {counts['ok']} added, {counts['updated']} updated, "
                          f"{counts['skip']} skipped, {counts['FAILED']} failed ===")
                self._log_next_steps(v, router)
                self.after(0, lambda: self._save(device, v, port_roles, host))
            except Exception as exc:
                self._log(f"[FAILED] {type(exc).__name__}: {exc}")
            finally:
                client.close()
                self.after(0, lambda: self._set_busy(False))

        threading.Thread(target=worker, daemon=True).start()

    # ---- Upload GUI Files ------------------------------------------------

    def _start_upload(self):
        info = self.router_info
        if not info.get("pc_direct", True):
            mbox.showerror("Not a direct connection", "This PC is reaching the router through another router - "
                           "plug straight in and Test Connection again.")
            return
        if not configurator.board_matches(self.device_var.get(), info["board"]):
            mbox.showerror("Wrong router?", f"The router at {self.host_var.get()} reports \"{info['board']}\", "
                           "not the selected device. Test Connection again.")
            return
        host, user, password = self.host_var.get().strip(), self.user_var.get(), self.pass_var.get()
        expected_board = info["board"]
        self._set_busy(True)
        self._log(f"=== Uploading the customer GUI to {host} ===")

        def worker():
            client = MikrotikClient(host)
            try:
                client.connect(user, password)
                board = configurator.probe_router(client, host)["board"]
                if board != expected_board:
                    self._log(f"[STOPPED] The router changed since Test Connection ({expected_board} -> {board}).")
                    return
                count = gui_upload.upload_gui(client, host, user, password, GUI_DIR, self._log)
                self._log(f"=== Done: {count} files uploaded. Connect a phone to the hotspot WiFi - the "
                          "ZxheiFi login page should appear. ===")
            except gui_upload.UploadError as exc:
                self._log(f"[FAILED] {exc}")
            except Exception as exc:
                self._log(f"[FAILED] {type(exc).__name__}: {exc}")
            finally:
                client.close()
                self.after(0, lambda: self._set_busy(False))

        threading.Thread(target=worker, daemon=True).start()

    def _log_next_steps(self, v, router):
        self._log("")
        self._log("NEXT STEPS")
        self._log("1. Click 'Upload GUI Files' - it puts the login/status/admin pages into the router's "
                  f"'{router['html_dir']}' folder (manual way: see the Guide tab).")
        self._log("2. NodeMCU Setup Wizard (join the 'ZxheiFi-Setup' WiFi from your phone) - enter:")
        self._log(f"     WiFi SSID: {v['ssid']}    WiFi password: "
                  + (v['wpaPassword'] or '(leave blank - the WiFi is open)'))
        self._log(f"     MikroTik IP: 10.0.0.1    API username: {cmds.API_USER}    "
                  f"API password: {v['apiPassword']}")
        self._log("3. Customers join the WiFi and get the login page automatically.")
        self._log("4. From now on manage this router at ITS OWN address, not 192.168.88.1 (every MikroTik -")
        self._log("   including your main router - uses 192.168.88.1 by default):")
        self._log("     PC on a 'lan' port: set the PC's Ethernet back to automatic IP, Router Host = 10.0.20.1")
        self._log("     PC on a hotspot port: automatic IP, Router Host = 10.0.0.1")
        self._log("   Winbox: connect by MAC address (Neighbors tab) - that always reaches the router on the cable.")
        self._log(f"   This PC keeps its access at {router.get('mgmt_address', self.host_var.get())}.")

    def _save(self, device, v, port_roles, host):
        mine = {
            "device": device, "host": host, "vars": v,
            "features": {k: var.get() for k, var in self.check_vars.items()},
            "port_roles": {p: var.get() for p, var in self.port_role_vars.items()},
        }
        self.settings.update(mine)
        save_settings(mine)   # only this tab's keys - never the Guide's language

    # ---- Remove -----------------------------------------------------------

    def _start_remove(self):
        info = self.router_info
        if not mbox.askyesno("Remove ZxheiFi Config",
                             f"Remove everything ZxheiFi created on \"{info['identity']}\" "
                             f"({info['board']})?\n\nOnly objects ZxheiFi made are deleted (its bridge, "
                             "hotspot, PPPoE server, firewall/NAT rules, schedulers, API user and the "
                             "customer accounts on its speed profiles). DNS and WiFi settings are not "
                             "reverted.", icon="warning", default="no"):
            return
        host, user, password = self.host_var.get().strip(), self.user_var.get(), self.pass_var.get()
        expected_board = info["board"]
        self._set_busy(True)
        self._log(f"=== Removing ZxheiFi config from {host} ({expected_board}) ===")

        def worker():
            client = MikrotikClient(host)
            try:
                client.connect(user, password)
                board = configurator.probe_router(client, host)["board"]
                if board != expected_board:
                    self._log(f"[STOPPED] The router changed since Test Connection ({board}). Nothing removed.")
                    return
                n = configurator.remove_zxheifi(client, self._log)
                self._log(f"=== Removed {n} item(s) ===")
            except Exception as exc:
                self._log(f"[FAILED] {type(exc).__name__}: {exc}")
            finally:
                client.close()
                self.after(0, lambda: self._set_busy(False))

        threading.Thread(target=worker, daemon=True).start()


class GuideTab(ctk.CTkFrame):
    """Static walkthrough, read-only - content lives in guide_content.py
    so it can be edited without touching this layout code. English/Tagalog
    switch at the top; the choice is remembered in the settings file."""

    def __init__(self, master):
        super().__init__(master, fg_color="transparent")
        lang = load_settings().get("guide_lang", "tl")
        if lang not in guide_content.LANGUAGES:
            lang = "tl"
        self._codes = {name: code for code, (name, _) in guide_content.LANGUAGES.items()}

        bar = ctk.CTkFrame(self, fg_color="transparent")
        bar.pack(fill="x", padx=16, pady=(4, 8))
        ctk.CTkLabel(bar, text="Language / Wika:", text_color=theme.TEXT_DIM).pack(side="left", padx=(0, 8))
        self._switch = ctk.CTkSegmentedButton(
            bar, values=[name for name, _ in guide_content.LANGUAGES.values()],
            command=self._on_language, selected_color=theme.ACCENT,
            selected_hover_color=theme.ACCENT_DARK)
        self._switch.set(guide_content.LANGUAGES[lang][0])
        self._switch.pack(side="left")

        self._scroll = None
        self._render(lang)

    def _on_language(self, name):
        code = self._codes[name]
        save_settings({"guide_lang": code})
        self._render(code)

    def _render(self, code):
        if self._scroll is not None:
            self._scroll.destroy()
        scroll = self._scroll = ctk.CTkScrollableFrame(self, fg_color="transparent")
        scroll.pack(fill="both", expand=True)
        for title, body in guide_content.LANGUAGES[code][1]:
            section = ctk.CTkFrame(scroll, fg_color=theme.PANEL)
            section.pack(fill="x", padx=16, pady=(0, 12))
            ctk.CTkLabel(section, text=title, font=("", 15, "bold"),
                         text_color=theme.ACCENT, anchor="w").pack(
                fill="x", padx=14, pady=(12, 4))
            ctk.CTkLabel(section, text=body, font=("", 12), text_color=theme.TEXT,
                         anchor="w", justify="left", wraplength=760).pack(
                fill="x", padx=14, pady=(0, 14))


class App(ctk.CTk):
    def __init__(self):
        super().__init__()
        theme.apply()
        self.title(f"ZxheiFi Setup Companion v{APP_VERSION}")
        self.geometry("960x680")
        self.minsize(760, 480)
        self.configure(fg_color=theme.INK)
        try:
            self.iconbitmap(default="")
        except Exception:
            pass

        header = ctk.CTkFrame(self, fg_color="transparent")
        header.pack(fill="x", padx=16, pady=(16, 0))
        logo_path = os.path.join(ASSETS_DIR, "logo.png")
        if os.path.isfile(logo_path):
            from PIL import Image
            img = ctk.CTkImage(Image.open(logo_path), size=(40, 40))
            ctk.CTkLabel(header, image=img, text="").pack(side="left", padx=(0, 10))
        ctk.CTkLabel(header, text="ZxheiFi Setup Companion", font=("", 20, "bold"),
                     text_color=theme.TEXT).pack(side="left")

        tabs = ctk.CTkTabview(self, fg_color=theme.PANEL, segmented_button_selected_color=theme.ACCENT,
                              segmented_button_selected_hover_color=theme.ACCENT_DARK)
        tabs.pack(fill="both", expand=True, padx=16, pady=16)
        flash_tab = tabs.add("Flash Firmware")
        mikrotik_tab = tabs.add("Configure MikroTik")
        guide_tab = tabs.add("Guide")

        mikrotik_tab_instance = MikrotikTab(mikrotik_tab)
        mikrotik_tab_instance.pack(fill="both", expand=True)
        FlashTab(flash_tab, on_device_detected=mikrotik_tab_instance.apply_detected_device).pack(
            fill="both", expand=True)
        GuideTab(guide_tab).pack(fill="both", expand=True)


if __name__ == "__main__":
    # (--esptool-worker runs are dispatched at the top of this file.)
    App().mainloop()
