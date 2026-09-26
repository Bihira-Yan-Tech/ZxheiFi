# 🧙 First-Boot Setup Wizard

Flash the firmware once, then configure WiFi/MikroTik/admin credentials
from a phone or laptop — no Arduino IDE, no `config.h` editing, no
reflashing. Added 2026-09-04 after comparing this project against the
original [JuanFi](https://github.com/ivanalayan15/JuanFi), which ships
a similar wizard; ours previously required hand-editing `config.h` and
recompiling for every deployment, a real practical gap for anyone
selling/installing multiple units.

## How it works

**On a genuinely fresh device** (no `/network.json` saved yet), or
**whenever the onboard FLASH button is pressed while the blue LED
blinks fast in the first 3 seconds after boot** (the button most
NodeMCU dev boards already have wired to GPIO0 — no extra wiring
needed), the firmware skips normal operation entirely and instead:

1. Broadcasts its own WiFi network, `ZxheiFi-Setup` (open, no password).
2. Answers every DNS query on that network with its own address — the
   standard "captive portal" trick, so opening literally any website
   lands on the setup form instead (most phones/laptops will even
   auto-prompt "Sign in to network" the way public WiFi does).
3. Serves one form: WiFi SSID/password (the MikroTik's own hotspot
   network), MikroTik IP + API username/password, and the initial
   admin dashboard password.
4. On submit, saves everything to `/network.json` on the NodeMCU's
   flash and reboots — this time connecting to the real network with
   the credentials just entered.

Every field mirrors what used to live in `firmware/config.h` as
compile-time constants (`WIFI_SSID`, `WIFI_PASSWORD`, `NODEMCU_GATEWAY`,
`MIKROTIK_API_USER`, `MIKROTIK_API_PASS`, `ADMIN_PASSWORD_HASH`) —
those constants are now only a **fallback** for a device that's never
run the wizard (dev/testing builds, or firmware flashed before this
existed). See `firmware/network_config.h` (the saved-credentials
struct) and `firmware/setup_mode.h` (the AP + captive form itself).

## Reconfiguring later

Reset or power the device, then press FLASH while the blue LED blinks
fast (the first 3 seconds) to force it back into Setup Mode. Don't
hold it *while* powering on: GPIO0 low at power-on puts the ESP8266
into its ROM flashing mode, so the firmware never runs - that is why
the old "hold during boot" instruction never worked. This works even if it already has a saved config —
useful when moving the unit to a new location with a different WiFi
network, or recovering from a saved config that no longer works (wrong
password typed, MikroTik IP changed, etc.). There is deliberately **no
software-only reset** (e.g. a button in the Admin dashboard) — if the
saved config is bad enough that the device can't reach the network at
all, a dashboard button would be unreachable anyway, so the physical
button is the only recovery path that always works.

## Finding the device afterward: a router DNS name, not a hardcoded IP

The login page reaches the NodeMCU as `nodemcu.zxheifi.lan` - a static DNS entry the desktop app's Configure
MikroTik step (or the generated `.rsc`) adds on the router, pointing at
the NodeMCU's reserved DHCP address. It replaced the mDNS name
`zxheifi-nodemcu.local` on 2026-09-25: the captive-portal mini-browsers
on iPhones and most Androids don't resolve `.local` names, so the login
page couldn't reach the NodeMCU on exactly the phones that matter.

(The firmware still announces `zxheifi-nodemcu.local` over mDNS for a
PC on the same network.) `mikrotik/gui/script.js`'s `apiBase()`
targets that hostname instead of a hardcoded IP — the
NodeMCU's actual IP is whatever its network hands out via DHCP, and a
hardcoded IP only ever worked if someone separately configured a
matching static DHCP lease, a second place to keep in sync that's
exactly the kind of drift that caused the cross-origin bug documented
in [09-changelog.md](09-changelog.md). **`script.js`'s `NODEMCU_HOST`
and `desktop-app/mikrotik_commands.py`'s `NODEMCU_HOSTNAME` must match** — this
is a one-time constant, not something that drifts with the network, so
it doesn't reintroduce the same fragility.

(Historical note on the old mDNS name: mDNS resolution needs OS/browser
support - regular Android/iOS browsers handle it but their captive-portal
login windows don't, and Windows historically needs Bonjour (bundled with iTunes/some
printer software) or may not resolve `.local` names in a plain browser
without it. Since this address is used by the customer-facing GUI
(phones, overwhelmingly) rather than by IT staff on Windows laptops,
this is an acceptable tradeoff — the `.rsc` configs also still set up
a static DHCP lease for the NodeMCU (predictable IP, good practice
regardless), so a Windows admin who needs to reach it directly can use
that IP as a fallback.

## Backward compatibility

A device already running older firmware (pre-wizard) has no
`/network.json` and will enter Setup Mode on its very next boot after
upgrading — **which is correct**, since the old `config.h` constants
it was running with are gone; unless a network.json is created (via
the wizard) the wizard's own submit path re-derives the same
information. Keep the matching older GUI build (with the fixed-IP
`apiBase()`) uploaded until ready to also configure the new wizard flow,
since the mDNS name only resolves once the new firmware's `MDNS.begin()`
call has actually run.

**Not live-tested** — no real NodeMCU hardware available in this
session to confirm AP-mode captive portal behavior (DNS redirect,
OS auto-prompt detection) or mDNS resolution against real client
devices; verified by compilation only (423,991 bytes flash / 36,236
bytes RAM, `DNSServer`/`ESP8266mDNS` are both bundled with the
`esp8266` Arduino core, not new external dependencies).
