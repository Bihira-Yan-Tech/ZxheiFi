# ⚙️ hEX (RB750Gr3) Setup

The hEX has **no built-in WiFi**. It handles routing, the Hotspot/PPPoE
server, DHCP, and QoS, while a separate wireless AP (a plain access
point in bridge mode, or a second MikroTik in AP mode) broadcasts the
`ZXHEIFI` SSID to customers.

## 1. Wire it up
- WAN (ISP) → `ether1`
- External AP → any of `ether2`-`ether5` (all bridged as LAN)
- The external AP must be configured as a **dumb bridge/AP**, not a
  router — no DHCP server, no NAT of its own. It just forwards traffic
  to the hEX.

## 2. Apply the config
Follow `mikrotik/import_guide.md` using `mikrotik/hEX_full_config.rsc`.
Edit the same variable block as the hAP Lite guide, with hEX's higher
realistic ceiling for `bandwidthDown`/`bandwidthUp` (~300M/100M,
adjust to your actual ISP plan).

`hotspotInterface` is set to the LAN bridge (`bridge-local`) here
instead of a wlan interface, since the hEX's own ports are what the
external AP plugs into.

## 3. Reboot and verify
```
/system reboot
```
Since there's no `wlan1` to check, verify instead with:
```
/interface bridge port print   # ether2-ether6 should all show the LAN bridge
/ip hotspot print
/interface pppoe-server server print
```
Confirm the external AP is broadcasting `ZXHEIFI` and that a phone
connecting to it gets a `10.0.0.x` address (the hEX's DHCP, not the
AP's).

## 4. Point the NodeMCU at it
Same as the hAP Lite: set `WIFI_SSID`/`WIFI_PASSWORD` in
`firmware/config.h` to match, then continue to
[05-nodemcu-flash.md](05-nodemcu-flash.md).

## Extending coverage beyond one AP
Since the hEX already forces you to add an external AP, scaling
coverage further is just adding more of the same:

- **Indoor, multiple rooms/floors**: additional indoor APs
  (TP-Link EAP series, Ubiquiti UniFi, or another MikroTik in
  `station-bridge` mode), each bridged to the hEX's LAN, each
  broadcasting the same `ZXHEIFI` SSID.
- **Outdoor/long-range**: CPE units (TP-Link CPE210/510, Ubiquiti
  NanoStation, MikroTik wAP/LHG) — realistic range 50-200m+ depending
  on line-of-sight. Point-to-point back to the hEX, or point-to-
  multipoint from a rooftop CPE covering an open area (e.g. a
  basketball court or waiting shed).
- All of them must stay in **bridge mode** — the hEX is the only DHCP
  server and the only thing running the Hotspot/PPPoE service. An AP
  that hands out its own IPs will break login for anyone behind it.
- The hEX's ports are the practical ceiling: with 5 LAN ports, you can
  wire up to 5 AP/CPE units directly, or fewer if some ports are used
  for wired customer drops instead.

## Why choose hEX over hAP Lite
More CPU headroom for Gaming QoS and PPPoE at higher session counts,
and gigabit ports if your ISP plan exceeds what the hAP Lite's
100Mbit-class ports can pass. Trade-off: one more box (the external AP)
to buy, mount, and power.
