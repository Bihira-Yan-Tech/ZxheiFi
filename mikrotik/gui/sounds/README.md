# Sounds

A starter set is included: original sounds generated for ZxheiFi (the
insert-coin voice is Windows text-to-speech), about 380 KB in total, free
to use under the project's MIT license. To use your own, put audio files
here with **exactly** these names, click **Upload GUI Files** in the
desktop app (it uploads this folder too) and turn on **Admin → Settings →
Branding → Enable Sounds**. Every file is optional: a missing one is
simply silent. Only use music you have the right to play in your shop.

## The sound slots

| File | When it plays | Tip |
|---|---|---|
| `insert-coin.mp3` | Once, when the customer taps **Insert Coin** and the coin window opens | A short voice prompt, e.g. "Please insert your coins now." |
| `coin-drop.mp3` | Every time a coin is accepted and counted | Short "ka-ching", under 1 second |
| `coin-music.mp3` | **Loops while the coin window is open** (the countdown), stops when it closes | Upbeat music. The login-page background music pauses while this plays. |
| `success.mp3` | Connected: voucher accepted, coins turned into time, subscriber logged in | 1–2 seconds |
| `error.mp3` | Something failed: wrong/used voucher, coin slot busy, etc. | 1 second |
| `click.mp3` | Button taps | Very short and quiet - it plays often |
| `background-music.mp3` | Loops on the login/status pages after the customer's first tap (browsers don't allow sound before a tap) | Keep it soft |

## Getting it right

- **Format:** MP3 plays on every phone. Keep files small - about
  100–300 KB each (short clips), and 1–2 MB at most for the music loops.
  The router's storage is only a few MB (check Winbox → Files → free space).
- **Volume:** make them roughly equally loud; phones play at full media volume.
- **Test:** open the portal on a phone, tap the 🔇 speaker icon (turns 🔊),
  then tap Insert Coin.
- **Replacing a sound:** overwrite the file here with the same name and
  click Upload GUI Files again.

These files are served by the router itself, like `logo.png`, because
the customer's phone has no internet yet while it's on the login page.
