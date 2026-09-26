# Terms and Conditions

**Last updated: 2026-09-22**

These terms apply to anyone who downloads, builds, deploys, modifies, or
otherwise uses the ZxheiFi project ("the Software") — the firmware,
MikroTik configuration scripts, GUI, desktop companion app, voucher
tools, and documentation in this repository.

This document is a plain-language summary of expectations for using an
open-source project, written by the project maintainers — **it is not
legal advice**, and it does not replace the license. The [LICENSE](LICENSE)
file (MIT) is the actual legal grant of rights; if the two ever conflict,
the LICENSE file controls.

## 1. No warranty, use at your own risk

The Software is provided **"as is"**, with no warranty of any kind. The
MIT License already says this in legal terms (see LICENSE §2); this
section says it in plain terms: this is a community/personal project,
not a commercially supported product. Nobody is guaranteeing it is free
of bugs, that it will keep working after a MikroTik/RouterOS or
ESP8266/Arduino-core update, or that it is fit for any particular use.
**Test thoroughly on your own hardware before relying on it for a real
business.**

## 2. You are responsible for your own deployment

If you deploy this to collect money from real customers (coins,
vouchers, or subscriptions), **you, the operator, are solely
responsible** for:

- Complying with local laws and regulations that apply to your
  business and jurisdiction — this can include business registration,
  tax obligations, telecom/ISP-reseller rules, and consumer-protection
  rules, which vary by country and change over time. (For example, in
  the Philippines this can touch DTI/BIR business registration and
  NTC rules around reselling or providing internet access — the
  maintainers make no representation about what applies to your
  specific setup, and you should confirm with the relevant local
  authorities or a professional if unsure.)
- The accuracy of your own financial records. This software tracks
  sessions and logs events for its own operational purposes; it is
  not accounting or tax software, and the maintainers make no
  guarantee its numbers are suitable for tax filing or financial
  reporting without your own reconciliation.
- The electrical and physical safety of any hardware you build or
  wire (coin acceptors, mains-adjacent wiring, enclosures). If you are
  not confident wiring 220V/110V-adjacent equipment safely, hire
  someone who is.
- Your customers' data and privacy. This software's own footprint is
  small (voucher codes, session timestamps, no accounts beyond admin
  logins), but you are responsible for how you operate and secure the
  network you run it on.

## 3. No liability

To the fullest extent permitted by law, the maintainers and
contributors are not liable for any damages, losses, or costs arising
from your use of the Software — including lost revenue, hardware
damage, data loss, or regulatory penalties. This mirrors the MIT
License's liability disclaimer (§2) and does not create any additional
obligation beyond it.

## 4. Contributions and forks

You're welcome to fork, modify, and redistribute this project under the
terms of the MIT License, including for commercial use. If you
distribute a modified version, please keep the license notice intact
and avoid presenting your fork as an official ZxheiFi release unless it
actually is one — basic honesty, not a legal restriction beyond what
the license already requires.

## 5. Support

This is an open-source project maintained on a best-effort, volunteer
basis. There is no guaranteed response time for issues or pull
requests, and no guaranteed compatibility with future MikroTik/RouterOS
or ESP8266 Arduino core versions.

## 6. Changes to these terms

These terms may be updated as the project evolves. The version in the
`main` branch at any given time is the current one.
