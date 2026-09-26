"""
configurator.py - runs mikrotik_commands' Step plans against a live
router, plus the read-only probing that decides whether it's safe to.

Kept free of any Tk code so it can be exercised headless by
test_configurator.py against a fake router.
"""
import mikrotik_commands as cmds
from mikrotik_client import RouterOSError


# ---- Probing ---------------------------------------------------------------

def _first(client, path, filters=None):
    try:
        rows = client.print(path, filters)
    except RouterOSError:
        return {}
    return rows[0] if rows else {}


def _safe_rows(client, path, filters=None):
    try:
        return client.print(path, filters)
    except RouterOSError:
        return []


def probe_router(client, host):
    """Everything Config needs to know before touching the router: what
    it actually is, which of its ports carries this connection, and
    whether it already looks like someone's working router."""
    res = _first(client, "/system/resource")
    version = res.get("version", "?")
    try:
        ros_major = int(version.split(".")[0])
    except ValueError:
        ros_major = 7
    info = {
        "identity": _first(client, "/system/identity").get("name", "?"),
        "board": res.get("board-name", "?"),
        "version": version,
        "ros_major": ros_major,
        "arch": res.get("architecture-name", "?"),
    }

    ethers = [r["name"] for r in _safe_rows(client, "/interface/ethernet") if "name" in r]
    wireless = [r["name"] for r in _safe_rows(client, "/interface/wireless") if "name" in r]
    info["ports"] = ethers + wireless
    info["wireless"] = wireless

    addresses = _safe_rows(client, "/ip/address")
    for row in addresses:
        if row.get("address", "").split("/")[0] == host and row.get("invalid") != "true":
            info["mgmt_interface"] = row.get("actual-interface") or row.get("interface")
            info["mgmt_address"] = row.get("address")
            break

    # This PC's own address/MAC as the router sees it - only meaningful
    # when the PC is on the same wire (then the ARP entry is the PC's own
    # NIC, not some router in between).
    info["pc_ip"] = client.local_ip()
    # Is this PC on one of the router's own networks? If not, the session
    # is being routed through something else - on 2026-09-25 a WiFi path
    # reached the upstream hEX, which also answers at 192.168.88.1.
    info["pc_direct"] = _pc_on_router_network(info["pc_ip"], addresses)
    arp = _first(client, "/ip/arp", {"address": info["pc_ip"]}) if info["pc_ip"] else {}
    info["pc_mac"] = arp.get("mac-address")

    # Hotspot files live under "flash/" only on boards whose root is a
    # RAM disk - use whichever this router actually has, and tell the
    # user the same folder to upload the GUI into.
    flash = _safe_rows(client, "/file", {"name": "flash"})
    info["html_dir"] = "flash/hotspot" if flash else "hotspot"

    packages = {r.get("name") for r in _safe_rows(client, "/system/package")}
    info["has_hotspot_pkg"] = bool(_safe_rows(client, "/ip/hotspot/profile")) or "hotspot" in packages

    # RouterOS 7 "device-mode" (home mode is the default on many boards)
    # can forbid the hotspot and the scheduler. Config then "succeeds" but
    # the hotspot sits inactive ("not allowed by device-mode") and phones
    # land on WebFig instead of the login page. Empty on RouterOS 6.
    info["device_mode"] = _first(client, "/system/device-mode")

    info["foreign"] = foreign_config(client)
    return info


def _pc_on_router_network(pc_ip, address_rows):
    import ipaddress
    if not pc_ip:
        return True   # unknown - don't block on it
    try:
        ip = ipaddress.ip_address(pc_ip)
    except ValueError:
        return True
    for row in address_rows:
        try:
            if row.get("invalid") != "true" and ip in ipaddress.ip_interface(row.get("address", "")).network:
                return True
        except ValueError:
            continue
    return False


SCHEDULER_FEATURES = ("daily_reboot", "random_mac_fix")


def device_mode_blocks(info, feature_keys):
    """Which device-mode switches stop the selected features: a subset of
    ["hotspot", "scheduler"]."""
    dm = info.get("device_mode") or {}
    off = lambda key: str(dm.get(key, "")).lower() in ("no", "false")
    blocked = []
    if off("hotspot") and "hotspot" in feature_keys:
        blocked.append("hotspot")
    if off("scheduler") and any(k in feature_keys for k in SCHEDULER_FEATURES):
        blocked.append("scheduler")
    return blocked


def device_mode_command(blocked):
    return "/system device-mode update " + " ".join(f"{k}=yes" for k in blocked)


def foreign_config(client):
    """Counts config that ZxheiFi didn't create - a router with PPP
    accounts, its own firewall or hotspot is almost certainly already
    serving someone, and configuring it is exactly the mistake that took
    a household offline on 2026-09-23."""
    found = {}
    checks = [
        ("PPPoE/PPP accounts", "/ppp/secret"),
        ("firewall rules", "/ip/firewall/filter"),
        ("NAT rules", "/ip/firewall/nat"),
        ("hotspot servers", "/ip/hotspot"),
        ("DHCP servers", "/ip/dhcp-server"),
    ]
    for label, path in checks:
        rows = [r for r in _safe_rows(client, path)
                if r.get("dynamic") != "true" and not cmds.is_own(path, r)
                and not _routeros_placeholder(r)]
        if rows:
            found[label] = len(rows)
    return found


def _routeros_placeholder(row):
    # RouterOS itself adds a disabled "place hotspot rules here" rule
    # (chain=unused-hs-chain) to filter and NAT the first time a hotspot
    # runs. Counting them made every ZxheiFi router look "already
    # configured by someone else".
    return row.get("chain") == "unused-hs-chain" or row.get("comment") == "place hotspot rules here"


def board_matches(device_type, board):
    return any(token.lower() in board.lower() for token in cmds.DEVICE_BOARDS[device_type])


def detect_device(board):
    for device, tokens in cmds.DEVICE_BOARDS.items():
        if any(t.lower() in board.lower() for t in tokens):
            return device
    return None


# ---- Execution -------------------------------------------------------------

DEVICE_MODE_HINT = (
    "      RouterOS device-mode is blocking this. In Winbox > New Terminal run:\n"
    "      /system device-mode update hotspot=yes scheduler=yes\n"
    "      then within 5 minutes press the router's reset button briefly (or unplug and\n"
    "      replug its power). After it reboots, click Config again.")


class StepResult:
    OK, UPDATED, SKIPPED, FAILED = "ok", "updated", "skip", "FAILED"


def _is_own_row(step, row):
    if step.owned:
        return True
    return row.get("comment", "").startswith(cmds.TAG)


def _words(prefix, attrs):
    return [prefix] + [f"={k}={v}" for k, v in attrs.items()]


def run_step(client, step):
    """Returns (status, detail)."""
    if step.kind == "set":
        attrs = dict(step.attrs)
        words = [f"{step.path}/set"]
        if step.target is not None:
            words.append(f"=numbers={step.target}")
        words += [f"={k}={v}" for k, v in attrs.items()]
        ok, _, error = client.run(words)
        return (StepResult.UPDATED, None) if ok else (StepResult.FAILED, error)

    if step.kind == "remove":
        rows = client.print(step.path, step.filters)
        if not rows:
            return StepResult.SKIPPED, "nothing to remove"
        for row in rows:
            ok, _, error = client.run([f"{step.path}/remove", f"=.id={row['.id']}"])
            if not ok:
                return StepResult.FAILED, error
        return StepResult.OK, f"removed {len(rows)}"

    # ensure
    filters = {k: step.attrs[k] for k in step.key}
    rows = client.print(step.path, filters) if filters else []
    if not rows:
        ok, _, error = client.run(_words(f"{step.path}/add", step.attrs))
        return (StepResult.OK, None) if ok else (StepResult.FAILED, error)
    row = rows[0]
    if not _is_own_row(step, row):
        return StepResult.SKIPPED, "already exists and wasn't created by ZxheiFi - left untouched"
    updates = {k: v for k, v in step.attrs.items() if k not in step.key}
    ok, _, error = client.run(_words(f"{step.path}/set", {".id": row[".id"], **updates}))
    return (StepResult.UPDATED, None) if ok else (StepResult.FAILED, error)


def execute(client, plan, log):
    """plan: [(feature_label, [Step, ...]), ...]. Returns counts per status."""
    counts = {s: 0 for s in (StepResult.OK, StepResult.UPDATED, StepResult.SKIPPED, StepResult.FAILED)}
    for label, steps in plan:
        log(f"--- {label} ---")
        for step in steps:
            try:
                status, detail = run_step(client, step)
            except (RouterOSError, OSError) as exc:
                # A bridge change can drop this very session - reconnect
                # and retry the step once before calling it failed.
                log(f"  [..] {step.desc} - connection dropped ({exc}), reconnecting...")
                client.reconnect()
                try:
                    status, detail = run_step(client, step)
                except (RouterOSError, OSError) as exc2:
                    status, detail = StepResult.FAILED, str(exc2)
            counts[status] += 1
            log(f"  [{status}] {step.desc}" + (f" - {detail}" if detail else ""))
            if status == StepResult.FAILED and detail and "device-mode" in detail:
                log(DEVICE_MODE_HINT)
            if step.reconnect_after:
                try:
                    client.run(["/system/identity/print"])
                except (RouterOSError, OSError):
                    log("  [..] reconnecting after the bridge change...")
                    client.reconnect()
    return counts


def remove_zxheifi(client, log):
    """Deletes only what this app created (see mikrotik_commands.is_own)."""
    removed = 0
    deferred = []
    # RouterOS won't delete a security profile an interface still uses:
    # put any radio on our open profile back on "default" first.
    for wlan in _safe_rows(client, "/interface/wireless"):
        if wlan.get("security-profile") == "zxheifi-open":
            try:
                client.run(["/interface/wireless/set", f"=.id={wlan['.id']}", "=security-profile=default"])
                log(f"  [updated] {wlan.get('name')} back on the default security profile")
            except (RouterOSError, OSError) as exc:
                log(f"  [FAILED] {wlan.get('name')} security profile - {exc}")
    for path in cmds.REMOVAL_ORDER:
        rows = [r for r in _safe_rows(client, path) if r.get("dynamic") != "true" and cmds.is_own(path, r)]
        if path == "/queue/tree":
            # children (parent is another of our trees) before parents
            rows.sort(key=lambda r: 0 if r.get("parent", "").startswith("zxheifi-") else 1)
        for row in rows:
            if path == "/ip/address" and row.get("comment") == f"{cmds.TAG}: setup access":
                deferred.append((path, row))
                continue
            removed += _remove_row(client, path, row, log)
    for path, row in deferred:
        removed += _remove_row(client, path, row, log)
    log("  Note: the router's DNS servers and WiFi settings are not reverted - "
        "set them yourself in Winbox if needed.")
    return removed


def _remove_row(client, path, row, log):
    label = row.get("name") or row.get("comment") or row.get("address") or row.get("interface") or row[".id"]
    try:
        ok, _, error = client.run([f"{path}/remove", f"=.id={row['.id']}"])
    except (RouterOSError, OSError):
        try:
            client.reconnect()
            ok, _, error = client.run([f"{path}/remove", f"=.id={row['.id']}"])
        except (RouterOSError, OSError) as exc:
            ok, error = False, str(exc)
    log(f"  [{'removed' if ok else 'FAILED'}] {path} {label}" + ("" if ok else f" - {error}"))
    return 1 if ok else 0
