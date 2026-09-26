"""
mikrotik_commands.py - RouterOS configuration plan for each checklist
feature in the Configure MikroTik tab.

Every feature builds a list of Step objects that configurator.py runs:

- "ensure" steps are idempotent: the executor looks the object up first
  (by the step's `key` fields) and only adds it if missing, updates it if
  it's one ZxheiFi created, and leaves it alone if it belongs to someone
  else. Re-running Config is therefore safe and doesn't pile up duplicate
  firewall/NAT rules the way plain /add did.
- Everything ZxheiFi creates is identifiable: a unique `zxheifi-` name, or
  a comment starting with TAG. That's what lets "Remove ZxheiFi Config"
  (see removal_targets()) undo exactly what this app did and nothing else.

Firewall design follows MikroTik's own defconf pattern (accept
established/related, drop invalid, drop what comes in from the WAN)
instead of the earlier "drop everything not explicitly allowed" rules -
those silently cut off PPPoE subscribers and plain-LAN ports, and left
the API/DNS/web admin reachable from the internet side.

mikrotik/*.rsc (the manual-import scripts) are GENERATED from this plan
by export_rsc.py - run it after changing anything here.
"""

TAG = "zxheifi"
NODEMCU_HOSTNAME = "nodemcu.zxheifi.lan"   # must match mikrotik/gui/script.js NODEMCU_HOST
HOTSPOT_DNS_NAME = "login.zxheifi.lan"
API_USER = "zxheifi-api"

DEFAULT_VARS = {
    "lanBridge": "zxheifi-lan",
    "ssid": "ZXHEIFI",
    # Blank = OPEN customer WiFi, like every piso-WiFi/JuanFi unit: the
    # hotspot login (voucher/coin) is the paywall. A WPA2 password on the
    # customer SSID was pointless friction - and any phone's "share WiFi"
    # QR code handed it out anyway.
    "wpaPassword": "",
    "nodemcuIP": "10.0.0.254",
    "nodemcuMAC": "aa:bb:cc:dd:ee:ff",
    "apiPassword": "",
    "bandwidthDown": "70M",
    "bandwidthUp": "20M",
}

DEVICE_DEFAULTS = {
    "hap_lite": {"bandwidthDown": "70M", "bandwidthUp": "20M"},
    "hex": {"bandwidthDown": "300M", "bandwidthUp": "100M"},
}

# Fallback port lists, used only until Test Connection reads the real
# router's interfaces (see configurator.probe_router()). hAP lite
# (RB941-2nD) has 4 ethernet ports + wlan1; hEX (RB750Gr3) has 5 and no
# radio. (Older lists had a non-existent ether5 on the hAP lite and ether6
# on the hEX.)
DEVICE_PORTS = {
    "hap_lite": ["ether1", "ether2", "ether3", "ether4", "wlan1"],
    "hex": ["ether1", "ether2", "ether3", "ether4", "ether5"],
}

# Board-name substrings RouterOS reports in /system/resource for each
# supported device - used to refuse configuring a router that isn't the
# one selected (the incident this check exists for: the app reached a
# production hEX through a second network path while "hAP lite" was
# selected).
DEVICE_BOARDS = {
    "hap_lite": ("RB941", "hAP lite"),
    "hex": ("RB750Gr3", "hEX"),
}

LEGACY_COMMENTS = ("Drop all other input", "Drop other forward")


class Step:
    def __init__(self, desc, path, attrs=None, kind="ensure", key=(), owned=False,
                 target=None, filters=None, reconnect_after=False):
        self.desc = desc
        self.path = path              # e.g. "/ip/pool"
        self.attrs = attrs or {}      # RouterOS field -> value
        self.kind = kind              # "ensure" | "set" | "remove"
        self.key = tuple(key)         # attrs that identify an existing object
        # True when the key value itself is unique to ZxheiFi (a zxheifi-
        # name, our service-name...), so a match is ours by definition.
        # False when the key could collide with the owner's own objects
        # (an address, an interface) - then only a TAG comment proves it.
        self.owned = owned
        self.target = target          # "set": the numbers= value, e.g. "default"
        self.filters = filters or {}  # "remove": exact-match query
        self.reconnect_after = reconnect_after

    def __repr__(self):
        return f"Step({self.kind} {self.path} {self.attrs or self.filters})"


def _named(desc, path, name, **attrs):
    attrs = {"name": name, **{k.replace("_", "-"): v for k, v in attrs.items()}}
    return Step(desc, path, attrs, key=("name",), owned=True)


def _commented(desc, path, comment, **attrs):
    attrs = {**{k.replace("_", "-"): v for k, v in attrs.items()}, "comment": f"{TAG}: {comment}"}
    return Step(desc, path, attrs, key=("comment",), owned=True)


def default_port_roles(device_type, ports=None):
    ports = ports or DEVICE_PORTS[device_type]
    roles = {p: "hotspot" for p in ports}
    if "ether1" in roles:
        roles["ether1"] = "wan"
    return roles


def _ports_with_role(port_roles, role):
    return [p for p, r in port_roles.items() if r == role]


def _wan(port_roles):
    return (_ports_with_role(port_roles, "wan") or ["ether1"])[0]


def hotspot_interface(v, port_roles):
    """The interface the hotspot (and, without a dedicated port, PPPoE)
    runs on: the bridge if 2+ ports share the "hotspot" role, otherwise
    that single port directly."""
    ports = _ports_with_role(port_roles, "hotspot")
    return ports[0] if len(ports) == 1 else v["lanBridge"]


def base_network(device_type, v, port_roles, router):
    wan = _wan(port_roles)
    hotspot_ports = _ports_with_role(port_roles, "hotspot")
    mgmt_iface = router.get("mgmt_interface")
    steps = []

    for legacy in LEGACY_COMMENTS:
        steps.append(Step(f"Remove old '{legacy}' rule (older ZxheiFi version)",
                          "/ip/firewall/filter", kind="remove", filters={"comment": legacy}))

    if len(hotspot_ports) > 1:
        steps.append(_named("Hotspot LAN bridge", "/interface/bridge", v["lanBridge"],
                            protocol_mode="none", comment=TAG))
        # The address this app is connected through dies the moment its
        # port joins the bridge (a bridged port's own IP stops answering).
        # Put the same address on the bridge first so the session survives.
        if mgmt_iface in hotspot_ports and router.get("mgmt_address"):
            steps.append(_commented("Keep setup access on the bridge", "/ip/address", "setup access",
                                    address=router["mgmt_address"], interface=v["lanBridge"]))
        for port in hotspot_ports:
            steps.append(Step(f"Add {port} to hotspot LAN bridge", "/interface/bridge/port",
                              {"bridge": v["lanBridge"], "interface": port, "comment": TAG},
                              key=("interface",), reconnect_after=(port == mgmt_iface)))
    lan = hotspot_interface(v, port_roles)

    steps.append(Step("WAN DHCP client", "/ip/dhcp-client",
                      {"interface": wan, "disabled": "no", "comment": f"{TAG}: WAN"}, key=("interface",)))
    steps.append(_commented("LAN gateway IP", "/ip/address", "LAN gateway",
                            address="10.0.0.1/24", interface=lan))
    steps.append(Step("Router DNS", "/ip/dns", {"servers": "8.8.8.8,1.1.1.1",
                                                "allow-remote-requests": "yes"}, kind="set"))
    steps.append(_named("DHCP pool", "/ip/pool", "zxheifi-dhcp-pool", ranges="10.0.0.100-10.0.0.200"))
    steps.append(_named("DHCP server", "/ip/dhcp-server", "zxheifi-dhcp", interface=lan,
                        address_pool="zxheifi-dhcp-pool", disabled="no"))
    steps.append(Step("DHCP network", "/ip/dhcp-server/network",
                      {"address": "10.0.0.0/24", "gateway": "10.0.0.1", "dns-server": "10.0.0.1",
                       "comment": TAG}, key=("address",)))
    steps.append(Step("NodeMCU static DHCP lease", "/ip/dhcp-server/lease",
                      {"address": v["nodemcuIP"], "mac-address": v["nodemcuMAC"],
                       "server": "zxheifi-dhcp", "comment": f"{TAG}: NodeMCU"}, key=("mac-address",)))
    steps.append(Step("NodeMCU DNS name", "/ip/dns/static",
                      {"name": NODEMCU_HOSTNAME, "address": v["nodemcuIP"], "comment": TAG},
                      key=("name",), owned=True))
    steps.append(_commented("Internet NAT", "/ip/firewall/nat", "internet NAT",
                            chain="srcnat", out_interface=wan, action="masquerade"))

    lan_ports = _ports_with_role(port_roles, "lan")
    if lan_ports:
        plain = lan_ports[0] if len(lan_ports) == 1 else "zxheifi-plainlan"
        if len(lan_ports) > 1:
            steps.append(_named("Plain-LAN bridge", "/interface/bridge", "zxheifi-plainlan",
                                protocol_mode="none", comment=TAG))
            for port in lan_ports:
                steps.append(Step(f"Add {port} to plain-LAN bridge", "/interface/bridge/port",
                                  {"bridge": "zxheifi-plainlan", "interface": port, "comment": TAG},
                                  key=("interface",), reconnect_after=(port == mgmt_iface)))
        steps.append(_commented("Plain-LAN gateway IP", "/ip/address", "plain LAN gateway",
                                address="10.0.20.1/24", interface=plain))
        steps.append(_named("Plain-LAN DHCP pool", "/ip/pool", "zxheifi-plainlan-pool",
                            ranges="10.0.20.10-10.0.20.200"))
        steps.append(_named("Plain-LAN DHCP server", "/ip/dhcp-server", "zxheifi-plainlan-dhcp",
                            interface=plain, address_pool="zxheifi-plainlan-pool", disabled="no"))
        steps.append(Step("Plain-LAN DHCP network", "/ip/dhcp-server/network",
                          {"address": "10.0.20.0/24", "gateway": "10.0.20.1", "dns-server": "10.0.20.1",
                           "comment": TAG}, key=("address",)))

    rules = [
        ("accept established (input)", dict(chain="input", action="accept",
                                            connection_state="established,related,untracked")),
        ("drop invalid (input)", dict(chain="input", action="drop", connection_state="invalid")),
        ("accept ICMP", dict(chain="input", action="accept", protocol="icmp")),
        ("drop input from WAN", dict(chain="input", action="drop", in_interface=wan)),
        ("accept established (forward)", dict(chain="forward", action="accept",
                                              connection_state="established,related,untracked")),
        ("drop invalid (forward)", dict(chain="forward", action="drop", connection_state="invalid")),
        ("drop new from WAN not DSTNATed", dict(chain="forward", action="drop", connection_state="new",
                                                connection_nat_state="!dstnat", in_interface=wan)),
    ]
    for comment, attrs in rules:
        steps.append(_commented(f"Firewall: {comment}", "/ip/firewall/filter", comment, **attrs))
    return steps


def wifi_setup(v, router):
    """hAP lite only. authentication-types and the pre-shared keys belong to
    the security PROFILE, not /interface/wireless itself (setting them on
    the interface fails with "unknown parameter"); wireless-protocol takes
    802.11 (vs MikroTik's Nstreme/NV2), not "802.11n" - band= already
    selects the n speeds.

    A blank WiFi password gives an OPEN network (piso-WiFi style - the
    hotspot login is what customers pay for); a password gives WPA2."""
    password = (v.get("wpaPassword") or "").strip()
    if password:
        security = Step("WiFi security profile (WPA2 password)", "/interface/wireless/security-profiles",
                        {"mode": "dynamic-keys", "authentication-types": "wpa2-psk",
                         "wpa-pre-shared-key": password, "wpa2-pre-shared-key": password},
                        kind="set", target="default")
        profile = "default"
    else:
        security = _named("Open WiFi (no password - customers pay on the login page)",
                          "/interface/wireless/security-profiles", "zxheifi-open", mode="none")
        profile = "zxheifi-open"
    return [
        security,
        Step("wlan1 as the ZxheiFi access point", "/interface/wireless",
             {"mode": "ap-bridge", "ssid": v["ssid"], "band": "2ghz-b/g/n",
              "channel-width": "20/40mhz-XX", "frequency": "auto", "wireless-protocol": "802.11",
              "distance": "indoors", "security-profile": profile, "disabled": "no"},
             kind="set", target="wlan1"),
    ]


def pc_on_hotspot(v, port_roles, router):
    iface = router.get("mgmt_interface")
    return bool(iface) and (iface in _ports_with_role(port_roles, "hotspot")
                            or iface == hotspot_interface(v, port_roles))


def hotspot(v, port_roles, router):
    lan = hotspot_interface(v, port_roles)
    steps = []
    # The installer's PC sits on a hotspot port too. The moment the
    # hotspot starts it becomes a not-logged-in client, and the hotspot's
    # own rules block not-logged-in clients from the router's API - Config
    # would cut itself off mid-run. Exempt this one PC (by MAC) first.
    if pc_on_hotspot(v, port_roles, router) and router.get("pc_mac"):
        steps.append(Step("Keep this PC's access (hotspot bypass)", "/ip/hotspot/ip-binding",
                          {"type": "bypassed", "mac-address": router["pc_mac"],
                           "comment": f"{TAG}: setup PC"}, key=("mac-address",)))
    return steps + [
        # The pool must exist before the server that references it.
        _named("Hotspot address pool", "/ip/pool", "zxheifi-hs-pool", ranges="10.0.0.10-10.0.0.99"),
        _named("Hotspot profile", "/ip/hotspot/profile", "zxheifi-hs", hotspot_address="10.0.0.1",
               dns_name=HOTSPOT_DNS_NAME, html_directory=router.get("html_dir", "hotspot"),
               login_by="http-chap,http-pap", http_cookie_lifetime="1d"),
        _named("Hotspot server", "/ip/hotspot", "zxheifi-hotspot", interface=lan,
               profile="zxheifi-hs", address_pool="zxheifi-hs-pool", idle_timeout="15m",
               keepalive_timeout="2m", disabled="no"),
        # These three names are what the firmware provisions customers
        # onto (firmware/qos.h) - they can't carry a zxheifi- prefix.
        _named("Hotspot speed profile 1", "/ip/hotspot/user/profile", "hs-default",
               rate_limit="5M/10M", shared_users="1", transparent_proxy="no"),
        _named("Hotspot speed profile 2", "/ip/hotspot/user/profile", "hs-gaming",
               rate_limit="10M/30M", shared_users="1", transparent_proxy="no"),
        _named("Hotspot speed profile 3", "/ip/hotspot/user/profile", "hs-high",
               rate_limit="20M/50M", shared_users="1", transparent_proxy="no"),
        Step("Let the NodeMCU bypass the hotspot", "/ip/hotspot/ip-binding",
             {"type": "bypassed", "address": v["nodemcuIP"], "mac-address": v["nodemcuMAC"],
              "comment": f"{TAG}: NodeMCU"}, key=("mac-address",)),
        # Without this, a customer who hasn't logged in yet can't reach the
        # NodeMCU at all - the login page's voucher/coin calls would be
        # caught by the captive portal like any other traffic.
        _commented("Let the login page reach the NodeMCU", "/ip/hotspot/walled-garden/ip",
                   "NodeMCU API", action="accept", dst_address=v["nodemcuIP"]),
    ]


def pppoe_server(v, port_roles, router):
    ports = _ports_with_role(port_roles, "pppoe")
    mgmt_iface = router.get("mgmt_interface")
    steps = []
    if len(ports) > 1:
        steps.append(_named("PPPoE bridge", "/interface/bridge", "zxheifi-pppoe",
                            protocol_mode="none", comment=TAG))
        for port in ports:
            steps.append(Step(f"Add {port} to PPPoE bridge", "/interface/bridge/port",
                              {"bridge": "zxheifi-pppoe", "interface": port, "comment": TAG},
                              key=("interface",), reconnect_after=(port == mgmt_iface)))
        iface = "zxheifi-pppoe"
    elif len(ports) == 1:
        iface = ports[0]
    else:
        iface = hotspot_interface(v, port_roles)

    common = dict(local_address="10.0.10.1", remote_address="zxheifi-pppoe-pool",
                  dns_server="10.0.10.1", use_compression="yes", use_encryption="yes",
                  use_mpls="no", only_one="yes", comment=TAG)
    steps += [
        _named("PPPoE address pool", "/ip/pool", "zxheifi-pppoe-pool", ranges="10.0.10.10-10.0.10.200"),
        _named("PPPoE speed profile 1", "/ppp/profile", "pppoe-default", rate_limit="5M/10M", **common),
        _named("PPPoE speed profile 2", "/ppp/profile", "pppoe-gaming", rate_limit="10M/30M", **common),
        _named("PPPoE speed profile 3", "/ppp/profile", "pppoe-high", rate_limit="20M/50M", **common),
        Step("PPPoE server", "/interface/pppoe-server/server",
             {"service-name": "ZxheiFi-PPPoE", "interface": iface, "default-profile": "pppoe-default",
              "authentication": "pap,chap,mschap1,mschap2", "one-session-per-host": "yes",
              "max-sessions": "50", "keepalive-timeout": "60", "disabled": "no"},
             key=("service-name",), owned=True),
    ]
    return steps


def gaming_qos(v, port_roles, router):
    """Priority only means something when a queue is actually full, so the
    gaming/other split hangs under a parent capped at the WAN's real speed
    (the Bandwidth Down/Up fields) - without that cap the old
    parent=global trees never engaged. fq-codel is RouterOS 7 only."""
    lan = hotspot_interface(v, port_roles)
    wan = _wan(port_roles)
    gaming = "3074,3478-3479,12000-12099,27000-27030,30000-30099"
    mobile = "5000-9999,13000-19999,21000-26999"
    steps = [
        Step("Remove old queue tree 'gaming-priority'", "/queue/tree", kind="remove",
             filters={"name": "gaming-priority"}),
        Step("Remove old queue tree 'default-traffic'", "/queue/tree", kind="remove",
             filters={"name": "default-traffic"}),
    ]
    if router.get("ros_major", 7) >= 7:
        steps.append(_named("fq-codel queue type", "/queue/type", "zxheifi-fq-codel", kind="fq-codel",
                            fq_codel_ecn="no", fq_codel_target="5ms", fq_codel_interval="100ms",
                            fq_codel_quantum="1514"))
        qtype = "zxheifi-fq-codel"
    else:
        qtype = "default"
    for chain, field, ports, label in (
            ("prerouting", "dst-port", gaming, "gaming UDP (consoles/PC)"),
            ("postrouting", "src-port", gaming, "gaming UDP return (consoles/PC)"),
            ("prerouting", "dst-port", mobile, "gaming UDP (mobile, approx range)"),
            ("postrouting", "src-port", mobile, "gaming UDP return (mobile, approx range)")):
        steps.append(Step(f"Mark {label}", "/ip/firewall/mangle",
                          {"chain": chain, "action": "mark-packet", "new-packet-mark": "gaming-udp",
                           "passthrough": "no", "protocol": "udp", field: ports,
                           "comment": f"{TAG}: {label}"}, key=("comment",), owned=True))
    for direction, parent, limit in (("down", lan, v["bandwidthDown"]), ("up", wan, v["bandwidthUp"])):
        steps += [
            _named(f"{direction.title()}load cap", "/queue/tree", f"zxheifi-{direction}",
                   parent=parent, max_limit=limit, comment=TAG),
            _named(f"{direction.title()}load: gaming first", "/queue/tree", f"zxheifi-{direction}-gaming",
                   parent=f"zxheifi-{direction}", packet_mark="gaming-udp", priority="1",
                   queue=qtype, comment=TAG),
            _named(f"{direction.title()}load: everything else", "/queue/tree", f"zxheifi-{direction}-other",
                   parent=f"zxheifi-{direction}", packet_mark="no-mark", priority="8",
                   queue=qtype, comment=TAG),
        ]
    return steps


def content_filtering(v, port_roles, router):
    lan = hotspot_interface(v, port_roles)
    return [
        Step("Router DNS: CleanBrowsing Family Filter", "/ip/dns",
             {"servers": "185.228.168.168,185.228.169.168", "allow-remote-requests": "yes"}, kind="set"),
        _commented("Force DNS through the router (UDP)", "/ip/firewall/nat", "force DNS (UDP)",
                   chain="dstnat", action="redirect", to_ports="53", protocol="udp",
                   dst_port="53", in_interface=lan),
        _commented("Force DNS through the router (TCP)", "/ip/firewall/nat", "force DNS (TCP)",
                   chain="dstnat", action="redirect", to_ports="53", protocol="tcp",
                   dst_port="53", in_interface=lan),
        _commented("Block DNS-over-TLS bypass", "/ip/firewall/filter", "block DNS-over-TLS",
                   chain="forward", action="drop", protocol="tcp", dst_port="853", in_interface=lan),
    ]


# With shared-users=1 RouterOS refuses a second login for the same user
# rather than creating a second session, and the firmware already kicks
# the stale session itself when a phone reconnects under a new MAC - so
# this is a belt-and-braces cleanup, off by default. :do/on-error keeps
# one vanished session from aborting the whole pass.
_RANDOM_MAC_SCRIPT = """:foreach s in=[/ip hotspot active find] do={
    :do {
        :local user [/ip hotspot active get $s user]
        :local mac [/ip hotspot active get $s mac-address]
        :foreach o in=[/ip hotspot active find user=$user] do={
            :if ([/ip hotspot active get $o mac-address] != $mac) do={
                /ip hotspot active remove $o
                :log info "ZxheiFi random MAC fix: removed stale session for $user"
            }
        }
    } on-error={}
}"""


def random_mac_fix(v, port_roles, router):
    return [
        _named("Random MAC fix script", "/system/script", "zxheifi-random-mac-fix",
               source=_RANDOM_MAC_SCRIPT, policy="read,write,test", comment=TAG),
        _named("Random MAC fix schedule (every 30s)", "/system/scheduler", "zxheifi-random-mac",
               interval="30s", on_event="zxheifi-random-mac-fix", policy="read,write,test",
               comment=TAG),
    ]


def daily_reboot(v, port_roles, router):
    return [
        _named("Daily reboot at 03:00", "/system/scheduler", "zxheifi-daily-reboot",
               interval="1d", start_time="03:00:00", on_event="/system reboot",
               policy="reboot,read,write,policy,test", comment=TAG),
    ]


def nodemcu_api_access(v, port_roles, router):
    # write is required: the firmware adds/removes hotspot users, PPPoE
    # secrets and ip-bindings (firmware/mikrotik_api.h). The old read-only
    # "api-read" group made every customer login fail to provision.
    return [
        _named("NodeMCU API permission group", "/user/group", API_USER,
               policy="read,write,api,test", comment=TAG),
        _named("NodeMCU API user", "/user", API_USER, group=API_USER,
               password=v["apiPassword"], comment=TAG),
        Step("Remove old read-only 'api-read' group", "/user/group", kind="remove",
             filters={"name": "api-read"}),
    ]


FEATURES = [
    {"key": "base_network", "label": "Base Network",
     "description": "WAN, LAN bridge, DHCP, DNS, NAT and a firewall that blocks the internet side "
                    "without cutting off your own LAN/PPPoE users.",
     "required": True, "hap_lite_only": False, "build": base_network},
    {"key": "wifi_setup", "label": "WiFi Setup",
     "description": "Configures the hAP lite's own radio as the customer-facing AP.",
     "required": True, "hap_lite_only": True, "build": lambda d, v, p, r: wifi_setup(v, r)},
    {"key": "hotspot", "label": "Hotspot",
     "description": "Hotspot server, login page, the 3 speed profiles vouchers/coins use, and the "
                    "walled-garden entry that lets the login page reach the NodeMCU.",
     "required": True, "hap_lite_only": False, "build": lambda d, v, p, r: hotspot(v, p, r)},
    {"key": "pppoe", "label": "PPPoE Server",
     "description": "Optional subscriber PPPoE mode - uses a port marked \"PPPoE\" if any, "
                    "otherwise shares the hotspot interface.",
     "required": False, "hap_lite_only": False, "build": lambda d, v, p, r: pppoe_server(v, p, r)},
    {"key": "gaming_qos", "label": "Gaming QoS",
     "description": "Shapes traffic to your Bandwidth Down/Up and lets game traffic go first. "
                    "Set those two fields to ~90% of your real internet speed.",
     "required": False, "hap_lite_only": False, "build": lambda d, v, p, r: gaming_qos(v, p, r)},
    {"key": "content_filtering", "label": "Content Filtering",
     "description": "Switches the router's DNS to CleanBrowsing Family Filter, forces customers "
                    "through it, and blocks the DNS-over-TLS bypass.",
     "required": False, "hap_lite_only": False, "build": lambda d, v, p, r: content_filtering(v, p, r)},
    {"key": "random_mac_fix", "label": "Random MAC Fix",
     "description": "Extra cleanup of stale sessions from phones that change MAC. The NodeMCU already "
                    "handles this itself; needs Scheduler enabled in RouterOS device-mode.",
     "required": False, "hap_lite_only": False, "build": lambda d, v, p, r: random_mac_fix(v, p, r)},
    {"key": "daily_reboot", "label": "Daily Reboot Scheduler",
     "description": "Reboots the router at 03:00 daily. Needs Scheduler enabled in RouterOS device-mode.",
     "required": False, "hap_lite_only": False, "build": lambda d, v, p, r: daily_reboot(v, p, r)},
    {"key": "nodemcu_api_access", "label": "NodeMCU API Access",
     "description": "Creates the account the NodeMCU logs in with (needs write access to create "
                    "customer logins).",
     "required": True, "hap_lite_only": False, "build": lambda d, v, p, r: nodemcu_api_access(v, p, r)},
]


# What "Remove ZxheiFi Config" deletes, in dependency order (users before
# their group, children before parents, servers before the pools/profiles
# they use, the bridge last). A row matches if its comment starts with
# TAG or its name is listed - never anything else.
OWN_NAMES = {
    "/user": {API_USER},
    "/user/group": {API_USER, "api-read"},
    "/queue/type": {"zxheifi-fq-codel", "fq_codel_default"},
    "/queue/tree": {"gaming-priority", "default-traffic"},
    "/ip/hotspot": {"zxheifi-hotspot"},
    "/ip/hotspot/user/profile": {"hs-default", "hs-gaming", "hs-high"},
    "/ip/hotspot/profile": {"zxheifi-hs"},
    "/ppp/profile": {"pppoe-default", "pppoe-gaming", "pppoe-high"},
    "/ip/dhcp-server": {"zxheifi-dhcp", "zxheifi-plainlan-dhcp"},
    "/ip/pool": {"zxheifi-dhcp-pool", "zxheifi-plainlan-pool", "zxheifi-hs-pool", "zxheifi-pppoe-pool"},
    "/interface/bridge": {"zxheifi-lan", "zxheifi-plainlan", "zxheifi-pppoe"},
    "/interface/wireless/security-profiles": {"zxheifi-open"},
}
OWN_SERVICE_NAMES = {"ZxheiFi-PPPoE"}
# Customer accounts the NodeMCU created on our speed profiles.
CUSTOMER_ACCOUNT_PATHS = {"/ip/hotspot/user": {"hs-default", "hs-gaming", "hs-high"},
                          "/ppp/secret": {"pppoe-default", "pppoe-gaming", "pppoe-high"}}

REMOVAL_ORDER = [
    "/system/scheduler", "/system/script", "/user", "/user/group",
    "/queue/tree", "/queue/type", "/ip/firewall/mangle", "/ip/firewall/filter",
    "/ip/firewall/nat", "/ip/hotspot/walled-garden/ip", "/ip/hotspot/ip-binding",
    "/ip/hotspot/user", "/ip/hotspot", "/ip/hotspot/user/profile", "/ip/hotspot/profile",
    "/interface/pppoe-server/server", "/ppp/secret", "/ppp/profile",
    "/ip/dhcp-server/lease", "/ip/dhcp-server/network", "/ip/dhcp-server", "/ip/pool",
    "/ip/dns/static", "/ip/dhcp-client", "/ip/address", "/interface/bridge/port",
    "/interface/bridge", "/interface/wireless/security-profiles",
]


# Speed profiles an admin adds in the NodeMCU's Settings > Speed Profiles
# are created on the router by the firmware (QoSManager::profileFor).
SPEED_PREFIXES = {"/ip/hotspot/user/profile": "zx-speed-", "/ppp/profile": "zx-pppoe-"}
CUSTOMER_PREFIXES = {"/ip/hotspot/user": "zx-speed-", "/ppp/secret": "zx-pppoe-"}


def is_own(path, row):
    if row.get("comment", "").startswith(TAG):
        return True
    if row.get("name") in OWN_NAMES.get(path, ()):
        return True
    if path in SPEED_PREFIXES and row.get("name", "").startswith(SPEED_PREFIXES[path]):
        return True
    if row.get("service-name") in OWN_SERVICE_NAMES:
        return True
    if row.get("profile") in CUSTOMER_ACCOUNT_PATHS.get(path, ()):
        return True
    if path in CUSTOMER_PREFIXES and row.get("profile", "").startswith(CUSTOMER_PREFIXES[path]):
        return True
    return False
