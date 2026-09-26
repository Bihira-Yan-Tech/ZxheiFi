"""
test_configurator.py - headless checks of configurator.py + mikrotik_commands.py
against FakeRouter, a small in-memory stand-in for the RouterOS API.

Run: python test_configurator.py   (exits non-zero on any failure)

FakeRouter models the behaviours the real incidents hinged on: duplicate
names/interfaces are rejected with a !trap, a hotspot server can't
reference a pool that doesn't exist yet, and an unknown menu (hotspot
package missing) answers "no such command prefix".
"""
import sys

import configurator
import mikrotik_commands as cmds
from mikrotik_client import RouterOSError

UNIQUE_BY = {
    "/interface/bridge": "name", "/ip/pool": "name", "/ip/dhcp-server": "name",
    "/ip/hotspot": "name", "/ip/hotspot/profile": "name", "/ip/hotspot/user/profile": "name",
    "/ppp/profile": "name", "/queue/type": "name", "/queue/tree": "name", "/system/script": "name",
    "/system/scheduler": "name", "/user": "name", "/user/group": "name", "/ip/dns/static": "name",
    "/interface/bridge/port": "interface", "/ip/dhcp-client": "interface",
    "/interface/pppoe-server/server": "service-name", "/ip/dhcp-server/lease": "mac-address",
}
REFERENCES = {  # path -> (field, path it must exist in)
    "/ip/hotspot": [("address-pool", "/ip/pool"), ("profile", "/ip/hotspot/profile")],
    "/ip/dhcp-server": [("address-pool", "/ip/pool")],
    "/user": [("group", "/user/group")],
}


class FakeRouter:
    def __init__(self, board="RB941-2nD", version="7.20.8", hotspot_pkg=True, foreign=False,
                 device_mode=None, ftp_disabled=True):
        self.next_id = 1
        self.tables = {}
        self.hotspot_pkg = hotspot_pkg
        self.singletons = {"/ip/dns": {}, "/system/identity": {"name": "MikroTik"},
                           "/system/resource": {"board-name": board, "version": version,
                                                "architecture-name": "smips"}}
        # Real port counts: hAP lite = ether1-4 + wlan1, hEX = ether1-5.
        hex_board = "RB750" in board
        for n in range(1, 6 if hex_board else 5):
            self._add("/interface/ethernet", {"name": f"ether{n}"})
        if not hex_board:
            self._add("/interface/wireless", {"name": "wlan1"})
        self._add("/interface/wireless/security-profiles", {"name": "default"})
        self._add("/ip/address", {"address": "192.168.88.1/24", "interface": "ether2"})
        self._add("/ip/arp", {"address": "192.168.88.10", "mac-address": "AA:BB:CC:00:11:22"})
        self._add("/file", {"name": "flash"})
        if device_mode is not None:   # RouterOS 6 has no /system/device-mode at all
            self.singletons["/system/device-mode"] = device_mode
        self._add("/ip/service", {"name": "ftp", "port": "21", "disabled": "true" if ftp_disabled else "false"})
        self._add("/system/package", {"name": "routeros"})
        if hotspot_pkg:
            self._add("/ip/hotspot/profile", {"name": "default"})
        if foreign:
            self._add("/ip/firewall/filter", {"chain": "input", "action": "accept", "comment": "defconf: x"})
            self._add("/ip/dhcp-client", {"interface": "ether1"})
            self._add("/ppp/secret", {"name": "aplaya", "profile": "10Mbps"})

    def _add(self, path, row):
        row = dict(row, **{".id": f"*{self.next_id}"})
        self.next_id += 1
        self.tables.setdefault(path, []).append(row)
        return row

    reset_html_runs = 0

    def _put_file(self, name, size):
        table = self.tables.setdefault("/file", [])
        table[:] = [r for r in table if r["name"] != name]
        self._add("/file", {"name": name, "size": str(size)})

    # MikrotikClient surface --------------------------------------------
    def local_ip(self):
        return "192.168.88.10"

    def reconnect(self):
        return True

    def print(self, path, filters=None):
        ok, rows, error = self.run([f"{path}/print"] + [f"?{k}={v}" for k, v in (filters or {}).items()])
        if not ok:
            raise RouterOSError(error)
        return rows

    def run(self, words):
        cmd, args = words[0], words[1:]
        path, action = cmd.rsplit("/", 1)
        if path.startswith("/ip/hotspot") and not self.hotspot_pkg:
            return False, [], "no such command prefix"
        if cmd == "/ip/hotspot/reset-html":
            # Like RouterOS: writes its default page set into the server's
            # profile folder.
            self.reset_html_runs += 1
            server = next(r for r in self.tables["/ip/hotspot"] if r["name"] == args[0].split("=", 2)[2])
            profile = next(r for r in self.tables["/ip/hotspot/profile"] if r["name"] == server["profile"])
            folder = profile["html-directory"]
            for name, size in (("login.html", 4423), ("rlogin.html", 877), ("alogin.html", 1094),
                               ("status.html", 2855), ("errors.txt", 3719), ("md5.js", 7168)):
                self._put_file(f"{folder}/{name}", size)
            return True, [], None
        attrs = {a[1:].split("=", 1)[0]: a[1:].split("=", 1)[1] for a in args if a.startswith("=")}
        query = {a[1:].split("=", 1)[0]: a[1:].split("=", 1)[1] for a in args if a.startswith("?")}
        if path in self.singletons:
            if action == "print":
                return True, [dict(self.singletons[path])], None
            self.singletons[path].update(attrs)
            return True, [], None
        table = self.tables.setdefault(path, [])
        if action == "print":
            return True, [dict(r) for r in table if all(r.get(k) == v for k, v in query.items())], None
        if action == "add":
            key = UNIQUE_BY.get(path)
            if key and any(r.get(key) == attrs.get(key) for r in table):
                return False, [], f"failure: already have such {key}"
            for field, ref_path in REFERENCES.get(path, []):
                if field in attrs and not any(r.get("name") == attrs[field]
                                              for r in self.tables.get(ref_path, [])):
                    return False, [], f"input does not match any value of {field}"
            self._add(path, attrs)
            return True, [], None
        target = attrs.pop(".id", None) or attrs.pop("numbers", None)
        rows = [r for r in table if r[".id"] == target or r.get("name") == target]
        if not rows:
            return False, [], "no such item"
        if action == "set":
            # RouterOS takes yes/no but prints booleans back as true/false.
            rows[0].update({k: {"yes": "true", "no": "false"}.get(v, v) if k == "disabled" else v
                            for k, v in attrs.items()})
        elif action == "remove":
            table.remove(rows[0])
        return True, [], None


V = dict(cmds.DEFAULT_VARS, nodemcuMAC="bc:dd:c2:47:a2:76", apiPassword="s3cret-pass")
FAILURES = []


def check(cond, msg):
    print(("  PASS " if cond else "  FAIL ") + msg)
    if not cond:
        FAILURES.append(msg)


def full_plan(router, info, device="hap_lite", roles=None):
    roles = roles or cmds.default_port_roles(device, info["ports"])
    return [(f["label"], f["build"](device, V, roles, info)) for f in cmds.FEATURES]


def run_plan(router, info, **kw):
    logs = []
    counts = configurator.execute(router, full_plan(router, info, **kw), logs.append)
    return counts, logs


def rows(router, path):
    return router.tables.get(path, [])


def test_fresh_router():
    print("fresh hAP lite (No Default Configuration + bootstrap IP on ether2)")
    r = FakeRouter()
    info = configurator.probe_router(r, "192.168.88.1")
    check(configurator.detect_device(info["board"]) == "hap_lite", "board detected as hAP lite")
    check(info["mgmt_interface"] == "ether2", "PC's session is on ether2")
    check(info["pc_mac"] == "AA:BB:CC:00:11:22", "PC MAC found via ARP")
    check(info["html_dir"] == "flash/hotspot", "hotspot folder follows the router's flash disk")
    check(info["foreign"] == {}, "no foreign config reported")

    counts, logs = run_plan(r, info)
    failed = [l for l in logs if "[FAILED]" in l]
    check(counts["FAILED"] == 0, f"no failed steps ({failed})")
    check(any(a.get("comment") == "zxheifi: setup access" and a["interface"] == "zxheifi-lan"
              for a in rows(r, "/ip/address")), "setup address moved onto the bridge")
    order = [s.desc for _, steps in full_plan(r, info) for s in steps]
    check(order.index("Keep setup access on the bridge") < order.index("Add ether2 to hotspot LAN bridge"),
          "setup address is added before ether2 joins the bridge")
    check(order.index("Keep this PC's access (hotspot bypass)") < order.index("Hotspot server"),
          "PC is exempted before the hotspot starts")
    group = [g for g in rows(r, "/user/group") if g["name"] == cmds.API_USER]
    check(group and "write" in group[0]["policy"], "API group has write permission")
    check(any(w.get("dst-address") == V["nodemcuIP"] for w in rows(r, "/ip/hotspot/walled-garden/ip")),
          "walled garden lets the login page reach the NodeMCU")
    check(any(s["name"] == cmds.NODEMCU_HOSTNAME for s in rows(r, "/ip/dns/static")),
          "NodeMCU DNS name registered")
    drops = [f for f in rows(r, "/ip/firewall/filter") if f.get("action") == "drop"]
    check(all(f.get("in-interface") == "ether1" or f.get("connection-state") == "invalid"
              or f.get("dst-port") == "853" for f in drops),
          "every drop rule is scoped to WAN/invalid/DoT (no blanket drop)")
    base_dns = [s for s in cmds.base_network("hap_lite", V, cmds.default_port_roles("hap_lite"), info)
                if s.path == "/ip/dns"]
    check(base_dns[0].attrs["servers"].startswith("8.8.8.8"),
          "Base Network alone leaves the router on normal DNS")
    check(r.singletons["/ip/dns"]["servers"].startswith("185.228"),
          "Content Filtering switches the router to CleanBrowsing")
    return r, info


def test_hotspot_placeholders_not_foreign():
    print("RouterOS's own 'place hotspot rules here' markers")
    r = FakeRouter()
    r._add("/ip/firewall/filter", {"chain": "unused-hs-chain", "action": "passthrough",
                                   "comment": "place hotspot rules here", "disabled": "true"})
    r._add("/ip/firewall/nat", {"chain": "unused-hs-chain", "action": "passthrough",
                                "comment": "place hotspot rules here", "disabled": "true"})
    check(configurator.probe_router(r, "192.168.88.1")["foreign"] == {},
          "hotspot placeholder rules don't count as someone else's config")


def test_rerun_is_idempotent(r, info):
    print("re-running Config on the same router")
    before = {p: len(t) for p, t in r.tables.items()}
    counts, logs = run_plan(r, info)
    after = {p: len(t) for p, t in r.tables.items()}
    check(counts["ok"] == 0, f"nothing added twice (added {counts['ok']})")
    check(counts["FAILED"] == 0, "no failures on re-run")
    check(before == after, "no table grew (no duplicate rules/objects)")


def test_foreign_router_left_alone():
    print("router that already serves someone (hEX-style)")
    r = FakeRouter(board="RB750Gr3", version="6.48.6", foreign=True)
    info = configurator.probe_router(r, "192.168.88.1")
    check(not configurator.board_matches("hap_lite", info["board"]), "hAP lite selection rejected for a hEX")
    check("PPPoE/PPP accounts" in info["foreign"], "existing PPP accounts reported")
    counts, logs = run_plan(r, info, device="hex")
    client = [c for c in rows(r, "/ip/dhcp-client") if c["interface"] == "ether1"]
    check(len(client) == 1 and "comment" not in client[0], "owner's WAN DHCP client untouched")
    check(any("left untouched" in l for l in logs), "skip is reported, not silent")
    check(not any(t.get("kind") == "fq-codel" for t in rows(r, "/queue/type")), "no fq-codel on RouterOS 6")
    configurator.remove_zxheifi(r, lambda _l: None)
    check(any(s["name"] == "aplaya" for s in rows(r, "/ppp/secret")), "owner's PPP account survives removal")
    check(any(f.get("comment") == "defconf: x" for f in rows(r, "/ip/firewall/filter")),
          "owner's firewall rule survives removal")


def test_removal(r):
    print("Remove ZxheiFi Config")
    # a speed profile the NodeMCU created from Settings > Speed Profiles, and a customer on it
    r._add("/ip/hotspot/user/profile", {"name": "zx-speed-4", "rate-limit": "1M/2M"})
    r._add("/ip/hotspot/user", {"name": "ZXABC123", "profile": "zx-speed-4"})
    removed = configurator.remove_zxheifi(r, lambda _l: None)
    left = [(p, row) for p, t in r.tables.items() for row in t if cmds.is_own(p, row)]
    check(removed > 0 and not left, f"everything ZxheiFi made is gone (left: {left})")
    check(any(a["address"] == "192.168.88.1/24" and a["interface"] == "ether2" for a in rows(r, "/ip/address")),
          "original bootstrap address kept")


def test_missing_hotspot_package():
    print("router without the hotspot package")
    r = FakeRouter(hotspot_pkg=False)
    info = configurator.probe_router(r, "192.168.88.1")
    check(info["has_hotspot_pkg"] is False, "missing hotspot package detected before Config")


def test_wifi_open_or_wpa2():
    print("customer WiFi: open by default (piso-WiFi style), WPA2 only when a password is set")
    r = FakeRouter()
    info = configurator.probe_router(r, "192.168.88.1")
    run_plan(r, info)   # V has the default blank wpaPassword
    wlan = [w for w in rows(r, "/interface/wireless") if w["name"] == "wlan1"][0]
    check(wlan.get("security-profile") == "zxheifi-open", "blank password -> wlan1 on the open profile")
    check(any(p.get("name") == "zxheifi-open" and p.get("mode") == "none"
              for p in rows(r, "/interface/wireless/security-profiles")), "open profile has no encryption")
    steps = cmds.wifi_setup(dict(V, wpaPassword="secret123"), info)
    check(steps[0].attrs.get("authentication-types") == "wpa2-psk" and steps[1].attrs["security-profile"] == "default",
          "a password -> WPA2 on the default profile")


def test_indirect_path():
    print("PC reaching the router through another router (WiFi -> Tenda -> hEX, 2026-09-25)")
    r = FakeRouter()
    check(configurator.probe_router(r, "192.168.88.1")["pc_direct"] is True, "PC on the router's own network = direct")
    r.local_ip = lambda: "192.168.0.101"   # the PC's WiFi address behind the Tenda
    check(configurator.probe_router(r, "192.168.88.1")["pc_direct"] is False, "PC on another network = not direct")


def test_device_mode():
    print("RouterOS device-mode (hAP lite 'home' mode blocked the hotspot on 2026-09-25)")
    keys = ["base_network", "hotspot", "daily_reboot"]
    home = FakeRouter(device_mode={"mode": "home", "hotspot": "no", "scheduler": "no"})
    info = configurator.probe_router(home, "192.168.88.1")
    blocked = configurator.device_mode_blocks(info, keys)
    check(blocked == ["hotspot", "scheduler"], f"home mode blocks hotspot + scheduler ({blocked})")
    check(configurator.device_mode_command(blocked) == "/system device-mode update hotspot=yes scheduler=yes",
          "fix command names exactly the blocked switches")
    check(configurator.device_mode_blocks(info, ["base_network", "hotspot"]) == ["hotspot"],
          "scheduler not reported when no scheduler feature is checked")
    ok = FakeRouter(device_mode={"mode": "home", "hotspot": "yes", "scheduler": "yes"})
    check(configurator.device_mode_blocks(configurator.probe_router(ok, "192.168.88.1"), keys) == [],
          "allowed device-mode passes")
    ros6 = FakeRouter(board="RB750Gr3", version="6.48.6")
    check(configurator.device_mode_blocks(configurator.probe_router(ros6, "192.168.88.1"), keys) == [],
          "RouterOS 6 (no device-mode menu) passes")


class FakeFTP:
    """ftplib.FTP stand-in that writes into a FakeRouter's /file table."""
    def __init__(self, router, password_ok=True):
        self.router, self.password_ok, self.stored, self.dirs = router, password_ok, [], []

    def connect(self, host, port, timeout=None):
        if self.router.tables["/ip/service"][0]["disabled"] == "true":
            raise ConnectionRefusedError("ftp service is off")

    def login(self, user, password):
        if not self.password_ok:
            import ftplib
            raise ftplib.error_perm("530 Login incorrect")

    def mkd(self, d):
        self.dirs.append(d)

    def storbinary(self, cmd, fh):
        name = cmd.split(" ", 1)[1]
        self.router._put_file(name, len(fh.read()))
        self.stored.append(name)

    def quit(self):
        pass

    def close(self):
        pass


def test_upload_gui():
    print("Upload GUI Files")
    import os
    import gui_upload
    gui_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mikrotik", "gui")
    files = [rel for rel, _ in gui_upload.gui_files(gui_dir)]
    check("login.html" in files and "admin.html" in files and "logo.png" in files, "GUI files found")
    check(not any(f.startswith("hotspot/") or f.endswith(".md") for f in files),
          "stale local 'hotspot' copy and README files are not uploaded")

    r = FakeRouter()
    try:
        gui_upload.upload_gui(r, "h", "admin", "pw", gui_dir, lambda _l: None, lambda: FakeFTP(r))
        check(False, "upload before Config is refused")
    except gui_upload.UploadError as exc:
        check("run Config" in str(exc), "upload before Config is refused with a clear reason")

    info = configurator.probe_router(r, "192.168.88.1")
    run_plan(r, info)
    ftp = FakeFTP(r)
    logs = []
    count = gui_upload.upload_gui(r, "h", "admin", "pw", gui_dir, logs.append, lambda: ftp)
    folder = [p for p in r.tables["/ip/hotspot/profile"] if p["name"] == "zxheifi-hs"][0]["html-directory"]
    check(count == len(files) and all(f"{folder}/{f}" in ftp.stored for f in files),
          f"every GUI file uploaded into the profile's folder '{folder}'")
    check(r.reset_html_runs == 1, "MikroTik default pages created once (reset-html)")
    names = {row["name"] for row in r.tables["/file"]}
    check(f"{folder}/rlogin.html" in names, "MikroTik's own rlogin.html kept alongside ours")
    check(r.tables["/ip/service"][0]["disabled"] == "true", "FTP service turned back off afterwards")
    gui_upload.upload_gui(r, "h", "admin", "pw", gui_dir, logs.append, lambda: FakeFTP(r))
    check(r.reset_html_runs == 1, "re-upload doesn't reset-html again (would wipe our pages)")

    bad = FakeRouter(ftp_disabled=False)
    run_plan(bad, configurator.probe_router(bad, "192.168.88.1"))
    try:
        gui_upload.upload_gui(bad, "h", "admin", "pw", gui_dir, lambda _l: None,
                              lambda: FakeFTP(bad, password_ok=False))
        check(False, "FTP login failure reported")
    except gui_upload.UploadError as exc:
        check("FTP" in str(exc), "FTP login failure reported as an UploadError")


if __name__ == "__main__":
    r, info = test_fresh_router()
    test_rerun_is_idempotent(r, info)
    test_removal(r)
    test_foreign_router_left_alone()
    test_missing_hotspot_package()
    test_hotspot_placeholders_not_foreign()
    test_wifi_open_or_wpa2()
    test_indirect_path()
    test_device_mode()
    test_upload_gui()
    print(f"\n{'ALL PASSED' if not FAILURES else f'{len(FAILURES)} FAILED'}")
    sys.exit(1 if FAILURES else 0)
