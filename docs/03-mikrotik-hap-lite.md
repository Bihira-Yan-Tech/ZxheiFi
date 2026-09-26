# ⚙️ hAP Lite (RB951Ui-2nD) Setup

The hAP Lite has its own WiFi radio (`wlan1`), so it can act as the
Hotspot/PPPoE access point on its own — no separate AP box needed.

## 1. Factory reset (recommended for a clean install)
Winbox → System → Reset Configuration → check "No Default Configuration" → Reset.

## 2. Apply the config
Follow `mikrotik/import_guide.md` (copy-paste or `/import`) using
`mikrotik/hAP_lite_full_config.rsc`. Before applying, edit the variables
listed at the top of the file:

| Variable | Why it matters |
|---|---|
| `wanInterface` | Must match the port your ISP modem/cable is on |
| `ssid` / `wpaPassword` | The network your customers and the NodeMCU both join |
| `nodemcuIP` / `nodemcuMAC` | Static lease so the router always knows where the NodeMCU is |
| `bandwidthDown` / `bandwidthUp` | hAP Lite's realistic ceiling is ~70M/20M — don't oversell past what the WAN link actually delivers |

## 3. Reboot and verify
```
/system reboot
```
Then check per `mikrotik/import_guide.md`'s "Verification Commands"
section — confirm `/ip hotspot print`, `/interface pppoe-server server
print`, and `/interface wireless print` (wlan1 should show `running`).

## 4. Point the NodeMCU at it
The NodeMCU joins the `ZXHEIFI` WiFi network as a client — set
`WIFI_SSID`/`WIFI_PASSWORD` in `firmware/config.h` to match `ssid`/
`wpaPassword` above, then continue to
[05-nodemcu-flash.md](05-nodemcu-flash.md).

## Extending coverage beyond one room
The hAP Lite's own radio covers roughly a typical small shop (~15-20m
indoors through walls). To reach further:

- **Cheapest: a second wireless AP in "station-bridge" mode.** Any
  spare router (even another hAP Lite) set to `wireless mode=station-
  bridge`, connected to the main hAP Lite by WiFi, with its own ports
  bridged to `bridge-local`. It repeats the same `ZXHEIFI` network —
  customers see one SSID, no separate login. This is the same pattern
  as the hEX's external-AP setup ([04-mikrotik-hex.md](04-mikrotik-hex.md)),
  just added on top of the hAP Lite instead of replacing its radio.
- **Outdoor/long-range: a CPE (Customer Premise Equipment) unit** —
  e.g. TP-Link CPE210/510, Ubiquiti NanoStation, or MikroTik's own
  wAP/LHG series — pointed at the hAP Lite and bridged the same way.
  These are the small dish/panel antennas mounted outside; realistic
  range is 50-200m+ depending on line-of-sight and the specific model.
- Keep every repeater/CPE in **bridge mode**, not router mode — they
  should not run their own DHCP server or NAT. `/ip dhcp-server print`
  on the hAP Lite should still show it as the only DHCP server on the
  network; if a repeater hands out its own IPs instead, hotspot login
  breaks for anyone connected through it.

## Known constraint
hAP Lite's CPU is modest — Gaming QoS (fq_codel + priority queues) adds
real load. If you see `/interface wireless print` show unstable
throughput under heavy multi-user load, consider the hEX instead (see
[04-mikrotik-hex.md](04-mikrotik-hex.md)), which offloads WiFi to a
separate AP and keeps the router CPU free for routing/queueing.
