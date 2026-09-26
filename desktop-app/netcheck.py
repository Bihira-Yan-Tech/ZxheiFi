"""
netcheck.py - the Configure MikroTik tab's "Network Check" button: the
same checks that found the 2026-09-25 "wrong password / Winbox keeps
logging out" problem, runnable by the owner without anyone's help and
without logging in to anything.

It looks at:
  * which of this PC's network adapters actually carries traffic to the
    router address (a WiFi path to 192.168.88.1 reached the upstream hEX,
    which uses the same MikroTik default address),
  * whether the Ethernet link keeps dropping or runs at 10 Mbps
    (Realtek "Green Ethernet"/EEE power saving did both),
  * every MikroTik that announces itself on the wire (MNDP, UDP 5678):
    name, board, RouterOS version, address and uptime - an uptime that
    resets means the router is rebooting,
  * whether the router answers ping.
"""
import ipaddress
import socket
import struct
import subprocess
import threading
import time

import psutil

MNDP_PORT = 5678


def route_for(host):
    """(local IP, adapter name) this PC would use to reach host - no packet
    is sent (a UDP 'connect' only asks Windows for the route)."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect((host, 9))
        local_ip = s.getsockname()[0]
        s.close()
    except OSError:
        return None, None
    for name, addrs in psutil.net_if_addrs().items():
        if any(a.family == socket.AF_INET and a.address == local_ip for a in addrs):
            return local_ip, name
    return local_ip, None


def same_subnet(ip_a, ip_b, prefix=24):
    try:
        return ipaddress.ip_interface(f"{ip_a}/{prefix}").network == ipaddress.ip_interface(f"{ip_b}/{prefix}").network
    except ValueError:
        return False


def is_wireless(adapter):
    name = (adapter or "").lower()
    return any(w in name for w in ("wi-fi", "wifi", "wlan", "wireless"))


def wrong_path_hint(host):
    """A short warning when this PC reaches `host` through something other
    than a cable on the same subnet - i.e. probably through another router
    (the 2026-09-25 case: WiFi -> Tenda -> hEX at the same 192.168.88.1).
    None when the path looks direct."""
    local_ip, adapter = route_for(host)
    if not local_ip:
        return None
    if same_subnet(local_ip, host) and not is_wireless(adapter):
        return None
    return (f"This PC reaches {host} through '{adapter or local_ip}' ({local_ip}), not a cable on the "
            f"same network - that can be a DIFFERENT router with the same address. Plug straight into the "
            f"router, or use the router's own LAN address (e.g. 10.0.20.1 / 10.0.0.1) instead of 192.168.88.1.")


def _decode_mndp(data):
    fields, pos = {}, 4
    while pos + 4 <= len(data):
        t, l = struct.unpack(">HH", data[pos:pos + 4])
        v = data[pos + 4:pos + 4 + l]
        pos += 4 + l
        if t == 1:
            fields["mac"] = v.hex(":")
        elif t == 5:
            fields["identity"] = v.decode(errors="replace")
        elif t == 7:
            fields["version"] = v.decode(errors="replace").split(" ")[0]
        elif t == 10 and l == 4:
            fields["uptime"] = struct.unpack("<I", v)[0]
        elif t == 12:
            fields["board"] = v.decode(errors="replace")
        elif t == 16:
            fields["iface"] = v.decode(errors="replace")
    return fields


def discover(seconds, results):
    """Collects MNDP announcements into results {mac: [(time, fields, ip)]}."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        s.bind(("", MNDP_PORT))
        s.settimeout(0.5)
    except OSError as exc:
        results["_error"] = str(exc)
        return
    end, last_ask = time.time() + seconds, 0
    while time.time() < end:
        if time.time() - last_ask > 5:
            try:
                s.sendto(b"\x00\x00\x00\x00", ("255.255.255.255", MNDP_PORT))  # "who's there?"
            except OSError:
                pass
            last_ask = time.time()
        try:
            data, addr = s.recvfrom(2048)
        except OSError:
            continue
        f = _decode_mndp(data)
        if f.get("mac") and "uptime" in f:
            results.setdefault(f["mac"], []).append((time.time(), f, addr[0]))
    s.close()


def _ping(host):
    flags = ["-n", "1", "-w", "800"] if psutil.WINDOWS else ["-c", "1", "-W", "1"]
    return subprocess.run(["ping", *flags, host], capture_output=True,
                          creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0)).returncode == 0


def _fmt_uptime(sec):
    d, rem = divmod(int(sec), 86400)
    h, rem = divmod(rem, 3600)
    m = rem // 60
    return (f"{d}d " if d else "") + f"{h}h {m}m"


def run_check(host, log, seconds=20, ethernet_name=None):
    """Watches for `seconds` and logs findings plus plain-language advice.
    Returns the list of problems found (empty = all good)."""
    problems = []
    log(f"--- Network Check ({seconds}s) - nothing is changed, nothing is logged in to ---")

    local_ip, adapter = route_for(host)
    log(f"  Path to {host}: via '{adapter}' ({local_ip})")
    hint = wrong_path_hint(host)
    if hint:
        problems.append("wrong_path")
        log(f"  [!] {hint}")

    stats = psutil.net_if_stats()
    wired = ethernet_name or (adapter if adapter and not is_wireless(adapter) else None)
    if not wired:
        wired = next((n for n, st in stats.items() if st.isup and not is_wireless(n)
                      and "loopback" not in n.lower() and "vethernet" not in n.lower()), None)

    found = {}
    t = threading.Thread(target=discover, args=(seconds, found), daemon=True)
    t.start()
    flaps, prev_up, answered, lost, speeds = 0, None, 0, 0, set()
    end = time.time() + seconds
    while time.time() < end:
        if wired:
            st = psutil.net_if_stats().get(wired)
            up = bool(st and st.isup)
            if prev_up is not None and up != prev_up:
                flaps += 1
            prev_up = up
            if st and st.isup and st.speed:
                speeds.add(st.speed)
        if _ping(host):
            answered += 1
        else:
            lost += 1
        time.sleep(0.5)
    t.join(2)

    if wired:
        log(f"  Cable adapter '{wired}': link dropped {flaps // 2 + flaps % 2} time(s); speed "
            f"{', '.join(f'{s} Mbps' for s in sorted(speeds)) or 'no link'}")
        slow = bool(speeds) and max(speeds) <= 10
        if flaps:
            problems.append("link_flapping")
            log("  [!] The cable connection keeps dropping. Usual cause: the network card's power saving.")
        elif slow:
            problems.append("slow_link")
            log("  [!] Link is only 10 Mbps (router ports do 100). Usual cause: the network card's power "
                "saving, or a bad cable - and it tends to start dropping too.")
        if flaps or slow:
            log("      Fix (PowerShell as Administrator, one line at a time), then run Network Check again:")
            for prop in ("Energy-Efficient Ethernet", "Green Ethernet", "Power Saving Mode"):
                log(f"        Set-NetAdapterAdvancedProperty -Name \"{wired}\" -DisplayName \"{prop}\" "
                    f"-DisplayValue \"Disabled\"")
            log("      Still bad after that? Try another LAN cable.")
    else:
        log("  No wired network adapter is up - plug the PC into the router with a LAN cable.")
        problems.append("no_cable")

    log(f"  Ping {host}: {answered} answered, {lost} lost")
    if answered == 0:
        problems.append("no_ping")
        log(f"  [!] Nothing answers at {host}. Check the address and which router port the cable is in.")
    elif lost:
        problems.append("ping_loss")

    if found.get("_error"):
        log(f"  (Couldn't listen for MikroTik announcements: {found['_error']})")
    devices = {mac: seen for mac, seen in found.items() if not mac.startswith("_")}
    if devices:
        log(f"  MikroTik routers heard on this PC's networks: {len(devices)}")
    for mac, seen in devices.items():
        f, ip = seen[-1][1], seen[-1][2]
        log(f"    - {f.get('identity', '?')}  {f.get('board', '?')}  RouterOS {f.get('version', '?')}  "
            f"at {ip} (its {f.get('iface', '?')}), up {_fmt_uptime(f['uptime'])}  [{mac}]")
        ups = [s[1]["uptime"] for s in seen]
        if any(b < a for a, b in zip(ups, ups[1:])) or f["uptime"] < 120:
            problems.append("rebooting")
            log("      [!] Its uptime reset - this router is (re)booting. Check its power adapter.")
    if len(devices) > 1:
        log("  [!] More than one MikroTik is reachable - make sure Router Host is the one you mean.")

    if not problems:
        log("  All good: direct cable, stable link, router answering.")
    log("--- end of Network Check ---")
    return problems
