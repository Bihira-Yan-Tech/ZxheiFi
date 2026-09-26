# 🪙 Sub Vendo (v2)

A **Sub Vendo** is an extra coin box. It is a NodeMCU plus a coin acceptor,
placed somewhere else (a corner store, another floor), that sells time for the
**same** ZxheiFi WiFi. It joins the WiFi of the access point at its spot.

The main unit still does everything else: prices (Rate Profiles), customer
reservations, sessions, MikroTik and sales. The sub only counts coins and
switches its acceptor's relay.

## Buod (Tagalog)

1. Admin → Vendos → **+ Add Vendo**. Makakakuha ka ng pairing code (15 minuto,
   isang gamit lang).
2. I-flash ang box: Setup Companion → Flash Firmware → Device type **Sub Vendo**.
3. Sa phone, kumonekta sa WiFi na **ZxheiFi-Sub-Setup**. Ilagay ang WiFi ng
   lugar at ang code.
4. Makikita itong 🟢 online sa Vendos. I-print ang **QR** sticker at idikit sa
   box.

**Mga limitasyon:**
- Kapag NodeMCU ang main unit, hanggang **3** sub vendo lang.
- Kapag ESP32 ang main unit, hanggang **10** (darating sa susunod na update).

## Wiring

The pins are the same as on the main unit
([11-coin-acceptor-wiring.md](11-coin-acceptor-wiring.md)):

| Part | Default pin |
|---|---|
| Coin acceptor signal | D5 |
| Relay (acceptor power) | D7 |
| Buzzer | D8 |
| Ground | acceptor, 12 V supply and NodeMCU tied together |

Different baseboard? Change the pins in Admin → Vendos → **Edit** (coin pin,
relay pin or "none", relay HIGH/LOW, pesos per pulse). The box picks up the
new values within a few seconds; no reflash is needed.

## Setup

1. **Add Vendo.** Admin → Vendos → **+ Add Vendo** (super admin only). Type a
   name and you get a pairing code like `K7QM-2XPA-9RTD`. It is valid for
   15 minutes and works once. At most two codes can wait at a time.
2. **Flash.** In the Setup Companion's Flash Firmware tab, set Device type to
   **Sub Vendo** and click Flash. The sub's MAC is *not* copied into Configure
   MikroTik, because sub vendos need no router setup.
3. **Wizard.** On a phone, join **ZxheiFi-Sub-Setup** (every web address opens
   the form) and enter three things:
   - the WiFi of the AP at this spot;
   - the main unit's address (default `nodemcu.zxheifi.lan`, which the
     MikroTik resolves);
   - the pairing code.

   Then tap Save. To open the wizard again later, press FLASH while the blue
   LED blinks fast right after power-on. Leave the code blank to keep the
   current pairing.
4. **Check.** In Admin → Vendos the box shows 🟢 online. Its LED is solid.
5. **Sticker.** Click **QR** in its row and print. Customers scan it; the
   login page opens with that box already picked. Without the sticker there
   is a "Coin box" dropdown, which remembers the last choice on each phone.

## How a sale works

1. At the box, the customer taps **Insert Coin** on the login page, with
   *Tindahan* selected. The main unit reserves that box for their phone.
2. On its next poll (at most 2 s later) the sub is told to switch its relay
   on, so the acceptor takes coins. The sub chirps.
3. Each coin is queued in the sub's flash, then sent to the main unit. The
   customer sees ₱ and minutes rise.
4. **Done - Connect** (or 45 s without a new coin) turns it into internet. The
   sale is recorded under that box.

## Money safety

- **Fail-closed.** The relay is ON only while the box is reserved *and* the
  main unit answered within the last 3 s. With no link, the acceptor rejects
  coins.
- **Nothing lost in transit.**
  - Each coin is saved on the sub before sending and removed only after the
    main unit's signed "ok" (up to 20 can wait).
  - Each coin has a sequence number, so a resend is never counted twice.
  - The main unit saves the number and the box total *before* crediting.
- **Right customer.**
  - Each reservation has an id. A coin delivered late tops up the session of
    the customer who paid it.
  - An old coin can never go to the next customer.
  - A coin nobody claims within 60 s is still counted in sales (the cash is in
    the box) and logged `coin_late_unclaimed`, so you can refund someone.
- **No spoofing.**
  - Every message is signed with a key derived from the pairing code. The key
    never crosses the open WiFi.
  - Message counters stop replays.
  - Wrong codes are rate-limited (5 per minute).
  - **Remove** or **Re-pair** kills the old key immediately.

## Day to day

- **Vendos tab:**
  - status (🟢 online / 🔴 offline N min);
  - today's income;
  - **coin box** (₱ since the last collection);
  - commission.
- **Collected:** press this when you empty a box. It records who, when and how
  much (see the Collections table) and resets the box to ₱0. Staff accounts
  may do this too.
- **Commission:** the location host's share, in Edit.
  - Sales → pick the box to see *Coins · Commission · Host share · Your
    share*.
  - **Export per-vendo CSV** gives one row per date and box.
- **Telegram:** when enabled, you get "Vendo X offline for 5 min" and "back
  online".

## LED and buzzer

| LED | Meaning |
|---|---|
| double blink | not paired, or removed/re-paired in Admin |
| slow blink | connecting to the WiFi / main unit |
| solid | online, idle |
| fast blink | a customer is inserting coins (relay on) |
| triple blink | coins can't be sent (queue nearly full). Check the WiFi |

Buzzer: one beep per coin, a chirp when a customer's window opens, three beeps
when paired.

## Troubleshooting

| Problem | Fix |
|---|---|
| Still double blink after setup | The code was wrong, expired or already used. Add Vendo (or **Re-pair**) again, reopen the wizard, enter the new code |
| 🔴 offline in Admin | The box has no WiFi, or its AP isn't on the ZxheiFi network. Open the Serial Monitor on the sub and look for `Pairing:` / `Coin ... acknowledged` |
| Customer: "That coin box is offline" | Use another box until it's back |
| Add Vendo disabled | Board limit reached (NodeMCU main unit: 3) |
| Coins not counted | A Rate Profile is needed for the exact coin value. Check "pesos per pulse" and the coin pin in Edit |
| Box lost or stolen | **Remove**: it can never talk to the main unit again. Its sales history stays |

## Protocol (for developers)

See `common/zx_protocol.h` (C++, shared by both firmwares) and
`tools/zx_protocol.py`. The endpoints are in `firmware/vendo_api.h`:

```
POST /api/vendo/pair    {mac, nonce}              signed with K0 = HMAC(code, "zxheifi-pair-v1")
POST /api/vendo/poll    {v, n}                    -> {n, relay, rid, fast, cfgVer, name}
POST /api/vendo/coin    {v, n, seq, peso, rid}    -> {n, ok[, dup]}
POST /api/vendo/config  {v, n}                    -> {n, cfgVer, name, coinPin, relayPin, relayActiveHigh, pesosPerPulse}
```

The signature is `X-ZX-Sig: hex(HMAC-SHA256(K, raw body))` on requests and
replies, with `K = HMAC(K0, "key|<id>|<mac>|<nonce>")`.

**Tests:**
- `tools/vendo_test.py` (92 checks against `tools/mock_server.py`)
- `tools/run_host_tests.py` (the C++ on the PC)
- `common/selftest` (the C++ on a board)

`tools/sub_sim.py` can act as a sub vendo against a real main unit:

```bash
python tools/sub_sim.py --host 10.0.0.254 --port 80 --code XXXX-XXXX-XXXX --coin 5
```

Design and plan: `docs/superpowers/specs/2026-09-26-vendos-framework-subvendo-design.md`,
`docs/superpowers/plans/2026-09-26-vendos-framework-subvendo.md`.
