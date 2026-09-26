"""
export_rsc.py - writes mikrotik/*_full_config.rsc from the SAME plan the
desktop app's Configure MikroTik tab runs (mikrotik_commands.FEATURES).

The .rsc files used to be maintained by hand next to the app's plan and
drifted: a blanket-drop firewall, a read-only API user, no walled
garden, the hotspot pool created after the server that needs it. Now
they are generated, so a manual import and a Config run can't differ.

Run: python export_rsc.py   (rewrites every .rsc file in mikrotik/)

Every command is wrapped so one failure is reported and the rest of the
import continues, and every "add" first checks whether the object is
already there - re-importing is safe, like re-running Config.
"""
from pathlib import Path

import mikrotik_commands as cmds

OUT_DIR = Path(__file__).resolve().parent.parent / "mikrotik"

# Editable values become RouterOS globals at the top of the script.
VARIABLES = [
    ("zxSsid", "ssid", "WiFi name customers see (hAP lite only)"),
    ("zxNodemcuIP", "nodemcuIP", "Address reserved for the NodeMCU"),
    ("zxNodemcuMAC", "nodemcuMAC", "NodeMCU MAC - the desktop app's Flasher tab shows it"),
    ("zxApiPass", "apiPassword", "Password the NodeMCU logs in with - enter the same one in its setup wizard"),
    ("zxDown", "bandwidthDown", "~90% of your real download speed"),
    ("zxUp", "bandwidthUp", "~90% of your real upload speed"),
]
PLACEHOLDERS = {"apiPassword": "CHANGE-ME-8plus"}

DEVICES = {
    "hap_lite": {
        "file": "hAP_lite_full_config.rsc",
        "title": "hAP lite (RB941-2nD)",
        "router": {"mgmt_interface": "ether2", "mgmt_address": "192.168.88.1/24",
                   "html_dir": "hotspot", "ros_major": 7},  # no flash/ folder on a hAP lite
    },
    "hex": {
        "file": "hEX_full_config.rsc",
        "title": "hEX (RB750Gr3)",
        "router": {"mgmt_interface": "ether2", "mgmt_address": "192.168.88.1/24",
                   "html_dir": "flash/hotspot", "ros_major": 7},  # the hEX keeps files under flash/
    },
}

# random_mac_fix stays off, as in the app (the firmware already handles it).
INCLUDED = ["base_network", "wifi_setup", "hotspot", "pppoe", "gaming_qos",
            "content_filtering", "daily_reboot", "nodemcu_api_access"]


def cli_path(path):
    return "/" + " ".join(path.strip("/").split("/"))


def quote(value):
    value = str(value)
    if value.startswith("$zx"):          # a variable reference - leave unquoted
        return value
    escaped = (value.replace("\\", "\\\\").replace('"', '\\"').replace("$", "\\$")
               .replace("\n", "\\n").replace("\t", "\\t"))
    return f'"{escaped}"'


def attr_list(attrs):
    return " ".join(f"{k}={quote(v)}" for k, v in attrs.items())


def where(fields):
    return " and ".join(f"{k}={quote(v)}" for k, v in fields.items())


def step_to_rsc(step):
    p = cli_path(step.path)
    if step.kind == "remove":
        body = f"{p} remove [{p} find where {where(step.filters)}]"
    elif step.kind == "set":
        target = f" [{p} find where name={quote(step.target)}]" if step.target else ""
        body = f"{p} set{target} {attr_list(step.attrs)}"
    else:
        match = where({k: step.attrs[k] for k in step.key})
        add = f"{p} add {attr_list(step.attrs)}"
        if step.owned:
            other = f"{p} set [{p} find where {match}] {attr_list(step.attrs)}"
        else:
            other = f':put "  skipped (already exists, not created by ZxheiFi): {step.desc}"'
        body = f":if ([:len [{p} find where {match}]] = 0) do={{ {add} }} else={{ {other} }}"
    desc = step.desc.replace('"', "'")
    # RouterOS only lets a script use a global it declares itself.
    used = [var for var, _, _ in VARIABLES if f"${var}" in body]
    declare = "".join(f":global {var}; " for var in used)
    return f'{declare}:do {{ {body} }} on-error={{ :put "  FAILED: {desc}" }}'


def _variable_lines(device, defaults, keys):
    if not keys:
        return []
    lines =["# ---- EDIT THESE -------------------------------------------------------------"]
    for var, key, note in VARIABLES:
        if key not in keys:
            continue
        lines.append(f":global {var} {quote(defaults[key])}")
        lines.append(f"#   ^ {note}")
    return lines + [""]


def _feature_lines(device, v, feature_keys):
    roles = cmds.default_port_roles(device)
    router = DEVICES[device]["router"]
    features = {f["key"]: f for f in cmds.FEATURES}
    lines = []
    for n, key in enumerate(feature_keys):
        feature = features[key]
        steps = feature["build"](device, v, roles, router)
        lines += ["# " + "=" * 76, f"# {n + 1}. {feature['label'].upper()}",
                  f"#    {feature['description']}", "# " + "=" * 76,
                  f':put "ZxheiFi: {feature["label"]}"']
        lines += [step_to_rsc(s) for s in steps]
        lines.append("")
    return lines


def _render(device, title, intro, feature_keys):
    v = dict(cmds.DEFAULT_VARS, **cmds.DEVICE_DEFAULTS[device])
    defaults = {key: PLACEHOLDERS.get(key, v[key]) for _, key, _ in VARIABLES}
    for var, key, _ in VARIABLES:
        v[key] = f"${var}"
    v["wpaPassword"] = ""   # open customer WiFi, piso-WiFi style (the login page is the paywall)
    body = _feature_lines(device, v, feature_keys)
    used = {key for var, key, _ in VARIABLES if any(f"${var}" in line for line in body)}
    return "\n".join(
        ["# " + "=" * 76, f"# ZXHEIFI - {title}", "# " + "=" * 76,
         "# GENERATED by desktop-app/export_rsc.py from the same plan the desktop",
         "# app's Configure MikroTik tab runs - edit mikrotik_commands.py, not this.",
         "#"] + [f"# {line}" if line else "#" for line in intro] + ["# " + "=" * 76, ""]
        + _variable_lines(device, defaults, used) + body
        + ["# Forget the edit-me values so they don't linger in the router's environment.",
           "/system script environment remove [find where name~\"^zx\"]",
           ':put "ZxheiFi: done. Look above for any FAILED lines."', ""])


def render(device):
    spec = DEVICES[device]
    features = {f["key"]: f for f in cmds.FEATURES}
    keys = [k for k in INCLUDED if not (features[k]["hap_lite_only"] and device != "hap_lite")]
    intro = [
        "The desktop app is the recommended way: it checks it is talking to the",
        "right router first and fills in the MAC/password values for you.",
        "",
        "Before importing:",
        '  1. Router reset with "No Default Configuration", RouterOS 7.x.',
        "  2. 192.168.88.1/24 added on ether2, PC plugged into ether2.",
        "  3. hotspot package installed (RouterOS 7 hAP lite needs the extra package).",
        "  4. Edit the values below.",
        "Import: upload this file in Winbox > Files, then in New Terminal:",
        f"  /import file-name={spec['file']}",
        f"Then upload mikrotik/gui/* into the router's {spec['router']['html_dir']} folder.",
        "",
        "Ports: ether1 = internet (WAN), all other ports"
        + (" + wlan1" if device == "hap_lite" else "") + " = hotspot.",
        "Daily reboot needs the scheduler allowed in device-mode (see the guide).",
    ]
    return _render(device, f"MIKROTIK {spec['title']} FULL CONFIG (manual import)", intro, keys)


# Single features, for adding one to a router that already has the full
# config (they use its zxheifi-lan bridge and ether1 WAN).
STANDALONE = {
    "gaming_qos_fq_codel.rsc": ("gaming_qos", "GAMING QoS (standalone)"),
    "pppoe_server_setup.rsc": ("pppoe", "PPPoE SERVER (standalone)"),
    "content_filter.rsc": ("content_filtering", "CONTENT FILTER (standalone)"),
}


def render_standalone(feature_key, title):
    intro = [
        "Adds just this feature to a router that already has the full ZxheiFi",
        "config (hAP_lite_full_config.rsc / hEX_full_config.rsc or the desktop app):",
        "it uses that config's zxheifi-lan bridge and ether1 as the WAN.",
        f"Import: /import file-name={next(f for f, (k, _) in STANDALONE.items() if k == feature_key)}",
    ]
    return _render("hap_lite", title, intro, [feature_key])


def main():
    outputs = {spec["file"]: render(device) for device, spec in DEVICES.items()}
    outputs.update({name: render_standalone(key, title) for name, (key, title) in STANDALONE.items()})
    for name, text in outputs.items():
        (OUT_DIR / name).write_text(text, encoding="utf-8", newline="\n")
        print("wrote", OUT_DIR / name)


if __name__ == "__main__":
    main()
