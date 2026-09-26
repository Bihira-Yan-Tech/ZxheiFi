# 🪙 Coin Acceptor Wiring & Configuration

Covers single-line, proportional-pulse coin acceptors — the common,
practical choice for a microcontroller-driven setup like this one,
since it needs only one GPIO regardless of how many denominations
(₱1/5/10/20) the acceptor takes.

If you have a multi-line acceptor (a separate wire per denomination)
instead, that's a different firmware design — see
[10-feature-roadmap.md](10-feature-roadmap.md) for the current status;
it's not what this build supports today.

## How "proportional pulse" works
The acceptor outputs a burst of pulses on one signal wire per coin
inserted, where the **number of pulses equals the coin's peso value**
— a ₱1 coin sends 1 pulse, a ₱5 coin sends 5, a ₱10 coin sends 10, a
₱20 coin sends 20. The firmware (`processCoinSlot()` in
`firmware/nodemcu_firmware.ino`) counts the pulses in a burst and
multiplies by `COIN_PULSE_VALUE_PHP` (`config.h`, default 1) to get
the peso credit.

**This must be configured on the acceptor itself** — most multi-coin
selector modules sold for PH piso-wifi builds (commonly sold as "4-way
coin selector" or "multi coin acceptor" boards) have a small DIP switch
bank or a programming button sequence for setting each coin channel's
pulse count. Check your specific model's manual; the usual pattern is:
put the acceptor in programming mode, drop one coin of a given
denomination, and the number of pulses you send it during programming
becomes that channel's output count going forward. Set every channel
so pulse count = peso value, and leave `COIN_PULSE_VALUE_PHP` at 1.

If your acceptor can only be configured as "N pulses per peso" for a
fixed N instead (i.e. it can't independently distinguish denominations,
just totals a value), that also works — set `COIN_PULSE_VALUE_PHP` to
match instead.

## Pins are set in the Admin Dashboard
**Admin → Settings → Coin Slot** (no reflash, applied on Save):

| Setting | Default | Choices |
|---|---|---|
| Coin signal pin | D5 | D1, D2, D5, D6, D7 |
| Relay pin (acceptor power) | D7 | D1, D2, D5, D6, D7, or *None* (acceptor always on) |
| Relay switches ON with | HIGH | HIGH or LOW (match the relay module's H/L jumper) |
| Pesos per pulse | 1 | 1-100 |

D0 (no interrupt) and D3/D4/D8 (boot pins) aren't offered. If the coin or
relay is moved onto D6, the admin LED on D6 is switched off.

## Wiring
| Acceptor wire | NodeMCU pin | Notes |
|---|---|---|
| COIN / pulse output | Coin signal pin (D5 by default) | See voltage note below |
| GND | GND | Common ground with the NodeMCU |
| VCC (acceptor power) | External 12V (or per your acceptor's spec) | **Not** the NodeMCU's 5V/3.3V rail — these draw more current than a NodeMCU regulator should supply |

## The relay: rejecting coins until "Insert Coin" is tapped
The relay switches the acceptor's 12V: `12V+ → relay COM`, `relay NO →
acceptor DC12V`. It turns on only while a customer holds the coin slot
(tapped **Insert Coin**) and turns off 60 s after the tap if no coin
comes, or 45 s after the last coin (the credit is then turned into a
session automatically). With the acceptor unpowered, coins drop straight
to the return slot. Relay module: IN → relay pin, VCC → VIN/5V, GND → GND.
With *None*, a coin dropped before tapping is kept for up to 60 s and
given to the next customer who taps Insert Coin.

**Voltage note:** many coin acceptors pulse at 12V (or open-collector,
which is fine) on the signal line. The ESP8266's GPIOs are 3.3V-only —
feeding a 12V pulse directly onto D5 can damage the chip. If your
acceptor's datasheet shows the pulse line swinging above 3.3V, add a
simple opto-isolator (e.g. PC817) or a resistor-divider + transistor
stage between the acceptor and D5, not a direct wire. If it's already
an open-collector/opto output rated for 3.3-5V logic, a direct
connection (with `PIN_COINSLOT` in `INPUT_PULLUP` mode, which the
firmware already sets) is fine.

## Firmware-side pulse validation
Two defensive checks in `config.h`/`nodemcu_firmware.ino` protect
against electrical noise and acceptor malfunctions rather than
crediting garbage as real money:

- **`MIN_PULSE_INTERVAL_US`** (default 5000 = 5ms): the interrupt
  handler ignores any pulse arriving faster than this after the last
  one. Real coin acceptor pulses are electromechanical/opto-isolated
  and land tens of milliseconds apart at minimum — anything faster is
  almost always contact bounce or line noise, not a second coin.
- **`MAX_PLAUSIBLE_PULSES`** (default 50): if a single burst (before
  `COIN_DEBOUNCE_MS` of silence) somehow produces more pulses than
  this, the whole burst is discarded and logged as `coin_anomaly` in
  the Activity Log (Admin dashboard → Logs tab) instead of being
  credited — more likely a shorted line, a stuck mechanism, or
  tampering than PHP50+ of real coins in one drop.

If legitimate coins are getting rejected by either check (e.g. your
acceptor's pulses are naturally closer together than 5ms, or a single
denomination genuinely needs more than 50 pulses because
`COIN_PULSE_VALUE_PHP` is set very low), adjust the constants in
`config.h` and rebuild.

## Testing
1. Flash the firmware, open the serial monitor (115200 baud).
2. Insert one coin at a time, smallest denomination first.
3. Tap **Insert Coin** on the portal (the relay clicks), drop a coin, and
   watch for `Coin slot: PHP N coin` — confirm N matches the coin's value.
4. If N is consistently off (e.g. always double, or always short by
   one), the acceptor's pulse programming needs adjustment, not the
   firmware.
5. Tap **Done - Connect**: the phone logs in with a new `ZX…` code and
   the buzzer sounded once per accepted coin. A coin dropped *without*
   tapping Insert Coin should be rejected (relay off).

## Peso → time/data rate (Settings-configurable)
Earlier builds required credit to reach a fixed tier price (₱10/20/30)
before granting anything — a lone ₱5 got nothing. That's gone: each
coin's exact peso value (pulse count) is looked up in Settings → Rate
Profiles — the SAME table vouchers use, so a ₱5 coin grants exactly
what a ₱5 voucher would (`AdminAPI::profileByPeso()`). Insert a ₱5
coin and the customer gets that profile's minutes/data immediately, no
threshold to reach first — and since each denomination has its own
row, a bigger coin isn't mathematically forced to be worth exactly
proportionally more than a smaller one. A coin value with no matching
Rate Profile is logged (`coin_unmatched`) and credited nothing, so
define a row for every real denomination the acceptor sends. Each
profile's `speedProfile` field picks which MikroTik bandwidth profile
that session uses. See [07-features-guide.md](07-features-guide.md)'s
Rate Profiles section for the full explanation.
