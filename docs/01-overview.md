# 📖 ZXHEIFI — OVERVIEW

## What Is ZxheiFi?
An internet monetization system combining **coinslot + QR/digital vouchers + PPPoE** for MikroTik Hotspot networks, inspired by the open-source JuanFi project.

## Quick Stats
- **Hardware**: NodeMCU ESP8266 + MikroTik (hAP Lite / hEX)
- **Modes**: Hotspot + PPPoE (dual operation)
- **Payment**: QR codes, digital vouchers, physical coinslot
- **License**: MIT
- **GUI Size**: <100KB (3 pages + shared assets)
- **Firmware Size**: <900KB (see firmware/config.h's SIZE CONSTRAINTS section)

## Core Architecture
```
[USER DEVICE] ←WiFi→ [MIKROTIK] ←Hotspot/PPPoE→ [NODEMCU ESP8266]
     ↓                     ↓                        ↓
  LOGIN/QR           BACKEND LAYER           LOGIC ENGINE
  Status Page        Hotspot + PPPoE         Voucher + Tracking
  Admin Dashboard    QoS + Time + Data        API + Recovery
```

## Key Features
1. **Dual Mode Operation** — Hotspot (portal) + PPPoE (native)
2. **MAC-Agnostic Sessions** — Same voucher = same time, any device
3. **Time + Data Combo** — Both deducted simultaneously
4. **Idle Auto-Pause** — No charge when idle >5min
5. **Night Promo** — ₱5/hr + double data (12AM-6AM)
6. **Gaming QoS** — fq_codel + UDP prioritization
7. **Power Recovery** — SPIFFS session save every 30s
8. **Health Monitor** — Auto-reconnect + daily reboot
9. **Admin Dashboard** — Sales + system status
10. **Promo Editor** — Dynamic pricing

## File Map
```
juanfi-enhanced/
├── firmware/          # NodeMCU ESP8266 code
├── mikrotik/          # MikroTik import scripts
│   └── gui/           # Frontend (HTML/CSS/JS) — uploaded to MikroTik, not the NodeMCU
├── vouchers/          # Voucher generation tools
└── docs/              # Documentation
```

## Getting Started
1. See [03-mikrotik-hap-lite.md](03-mikrotik-hap-lite.md) or
   [04-mikrotik-hex.md](04-mikrotik-hex.md) for MikroTik setup
2. See [05-nodemcu-flash.md](05-nodemcu-flash.md) for firmware flashing
3. See [06-gui-customize.md](06-gui-customize.md) for portal customization
4. See [07-features-guide.md](07-features-guide.md) for how each feature
   actually works under the hood
5. Stuck? See [08-faq-troubleshoot.md](08-faq-troubleshoot.md)
6. Wiring a coin acceptor? See [11-coin-acceptor-wiring.md](11-coin-acceptor-wiring.md)