# MikroTik Manual Import Guide

The **desktop app's Configure MikroTik tab is the recommended way** to set up
a router: before changing anything, it checks it's talking to the router you
selected, and it fills in the NodeMCU MAC and API password for you. Use
these scripts only if you can't run the app.

The `.rsc` files here are **generated** from the app's own plan
(`desktop-app/export_rsc.py`), so a manual import gives exactly the same
setup as the app. Don't edit them by hand. Change
`desktop-app/mikrotik_commands.py` and re-run `python export_rsc.py`.

| File | What it is |
|------|------------|
| `hAP_lite_full_config.rsc` | Everything, for a hAP lite (RB941-2nD) |
| `hEX_full_config.rsc` | Everything, for a hEX (RB750Gr3), with an external AP on a LAN port |
| `pppoe_server_setup.rsc`, `gaming_qos_fq_codel.rsc`, `content_filter.rsc` | One feature each, to add to a router that already has the full config |

## Before you import

1. **Use a spare router, not the one your house/shop runs on.** These
   scripts replace the firewall, DHCP and DNS setup.
2. Reset it: *System → Reset Configuration → No Default Configuration*.
   Connect with Winbox by **MAC address** afterwards.
3. Give it a setup address: `/ip address add address=192.168.88.1/24 interface=ether2`
   and plug the PC into ether2.
4. RouterOS 7 on a hAP lite: install the **hotspot** extra package first
   (*System → Packages*), then reboot.
5. Back up whatever is on it: `/system backup save name=pre-zxheifi`.

## Import

1. Open the `.rsc` in a text editor and change the values under
   **EDIT THESE**:

   | Value | What to put |
   |-------|-------------|
   | `zxSsid` / `zxWifiPass` | WiFi name and password (hAP lite only) |
   | `zxNodemcuIP` | Address reserved for the NodeMCU (default 10.0.0.254) |
   | `zxNodemcuMAC` | The NodeMCU's MAC (the app's Flasher tab shows it when you pick its COM port) |
   | `zxApiPass` | Password the NodeMCU logs in with, 8+ characters. Type the same one in the NodeMCU setup wizard |
   | `zxDown` / `zxUp` | About 90% of your real internet speed, e.g. `45M` / `9M` |

2. Winbox → **Files** → upload the file.
3. **New Terminal** → `/import file-name=hAP_lite_full_config.rsc`
4. Read the output. Each section prints its name; anything that didn't
   apply prints `FAILED: ...` and the rest keeps going. `skipped (already
   exists...)` means the router already had something there that ZxheiFi
   didn't create, and it was left alone.
5. Upload everything in `mikrotik/gui/` into the router's hotspot folder:
   `flash/hotspot` if the router has a `flash` folder in Files (e.g. hEX),
   otherwise `hotspot` (e.g. hAP lite). `/ip hotspot profile print` shows it
   as `html-directory`.

Importing the same file again is safe. It won't create duplicates.

## Common problems

| Symptom | Check |
|---------|-------|
| `FAILED: Hotspot ...` | Hotspot package not installed (`/system package print`) |
| `FAILED: Daily reboot ...` | Scheduler blocked by device-mode: `/system device-mode update scheduler=yes`, then press the router's button or power-cycle it within 5 minutes |
| Login page can't reach the NodeMCU | `/ip hotspot walled-garden ip print` should list the NodeMCU address, and `/ip dns static print` should show `nodemcu.zxheifi.lan` |
| NodeMCU "MikroTik unreachable" | `/ip hotspot ip-binding print` (NodeMCU bypassed?), and the `zxheifi-api` user/password match what's in the NodeMCU wizard |
| Lost Winbox access after import | Connect by MAC address in Winbox; `192.168.88.1` is kept on the `zxheifi-lan` bridge as "setup access" |

## Undo

The desktop app's **Remove ZxheiFi Config** button removes exactly what
these scripts or the app created (everything is tagged `zxheifi`) and
nothing else.
