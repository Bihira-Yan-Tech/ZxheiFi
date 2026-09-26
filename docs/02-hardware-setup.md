# 🔧 HARDWARE SETUP

## Required Components
| Component | Model | Qty |
|-----------|-------|-----|
| Router | MikroTik hAP Lite (RB951Ui-2nD) or hEX (RB750Gr3) | 1 |
| Controller | NodeMCU ESP8266 (ESP-12E/F) | 1 |
| Power | 5V/2A USB | 1 |
| Storage | SPIFFS 1MB+ (built-in) | - |

## Wiring Diagram
The ESP8266 NodeMCU has no Ethernet port — it only has WiFi, and it
connects to the MikroTik as a WiFi **station** (client), not the other
way around. The MikroTik's wlan1 radio is what actually broadcasts the
`ZXHEIFI` SSID (see `mikrotik/hAP_lite_full_config.rsc` section 2); the
NodeMCU joins that same network to reach the RouterOS API.

```
MIKROTIK (broadcasts "ZXHEIFI" SSID, WiFi AP)
     |                              \
     | WiFi (station)                \ WiFi (hotspot clients)
     |                                 \
NODEMCU ESP8266                    USER DEVICES (phones, laptops)
(joins ZXHEIFI as a client,
 talks to RouterOS API on
 10.0.0.1:8728, coinslot +
 relay wired to its GPIOs)
```

## MikroTik Configuration Checklist
- [ ] WAN interface configured (DHCP/PPPoE from ISP)
- [ ] LAN bridge: 10.0.0.0/24
- [ ] Hotspot server enabled
- [ ] PPPoE server enabled (on wlan1)
- [ ] DHCP server for LAN
- [ ] DNS: 8.8.8.8 + 1.1.1.1
- [ ] NAT masquerade on WAN
- [ ] Firewall: allow established, related
- [ ] Scheduler: daily reboot 03:00

## NodeMCU Wiring
| NodeMCU Pin | Connection |
|-------------|-----------|
| VU / 5V | 5V Power |
| GND | GND |
| D5 (GPIO14) | Coin slot pulse output |
| D4 (GPIO2) | Status LED (built-in) |
| D6 (GPIO12) | Admin/MikroTik-link indicator LED |
| D7 (GPIO13) | Relay (external coin lock, optional) |
| D8 (GPIO15) | Buzzer |

There is no wired link to the MikroTik — the NodeMCU reaches it entirely
over WiFi (station mode, joining the `ZXHEIFI` SSID the MikroTik
broadcasts). TX/RX are left free for USB serial debugging only.

## Safety Notes
- Use 5V/2A regulated power for NodeMCU
- Avoid static discharge on ESP8266
- MikroTik needs proper cooling
- Keep firmware updated