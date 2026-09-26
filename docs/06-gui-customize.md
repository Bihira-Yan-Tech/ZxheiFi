# 🎨 Customizing the GUI

The GUI (`mikrotik/gui/login.html`, `status.html`, `admin.html`,
`print.html`, `style.css`, `script.js`, `qrcode.min.js`, `logo.png`,
`sounds/`) is uploaded to the **MikroTik**, not the
NodeMCU — the router serves the pages, and they talk to the NodeMCU's
JSON API (`firmware/gui_handler.h`) over AJAX for anything dynamic.
Because the GUI and the API live on two different devices, every
`/api/*` call goes through `script.js`'s `apiBase()` to target the
NodeMCU explicitly (`NODEMCU_HOST`, default `nodemcu.zxheifi.lan` — a
static DNS entry on the router, must match
`desktop-app/mikrotik_commands.py`'s `NODEMCU_HOSTNAME`) rather than a same-origin relative path — see the
`apiBase()` comment at the top of `script.js`, `firmware/gui_handler.h`'s
CORS headers, and [12-setup-wizard.md](12-setup-wizard.md) for the full
explanation. **If you change `NODEMCU_HOSTNAME` in `mikrotik_commands.py`, update
the matching constant in `script.js` too** - they must stay in sync or
the GUI silently can't reach the API in production.

## Uploading
Easiest: the Setup Companion's **Configure MikroTik → Upload GUI Files**
button. It reads the hotspot profile's `html-directory` (`hotspot` on a
hAP lite, `flash/hotspot` on a hEX), runs `/ip hotspot reset-html` if the
folder has no default pages yet, uploads every file over the router's
FTP (enabling the service briefly and restoring it after) and verifies
each file's size (`desktop-app/gui_upload.py`).

Manual: Winbox → Files → drag everything in `mikrotik/gui/` (including
`sounds/`) into that folder and overwrite. After any change, bump
`ZX_GUI_VERSION` in `script.js` **and** the `var want = '...'` guard in
each HTML page together — see Version Checks in
[07-features-guide.md](07-features-guide.md).

## Branding
- Colors: `style.css`'s `:root` — `--accent`/`--accent-dark` (default
  `#e0a339`/`#b8801f`, a warm brass tone) are the two rebrandable
  colors, overridable live from Settings without editing CSS at all
  (see `script.js`'s `applyBranding()`). `--peso` (success/money) and
  `--alert` (danger/expired) are intentionally NOT rebrandable — they
  signal a fixed meaning, not brand identity.
- Logo: each HTML file embeds the logo as an inline SVG data URI in the
  `.logo img` tag — swap it for your own SVG/PNG (base64-encode a PNG
  with `python -c "import base64;print(base64.b64encode(open('logo.png','rb').read()).decode())"`
  and use `data:image/png;base64,...`).
- Prices are never hardcoded in the pages: the voucher dropdown, the
  login page's price list and the print sheet all come live from
  Settings → Rate Profiles / Speed Profiles.

## Sounds (background music + effects)
`login.html`/`status.html` can play background music and short sound
effects (insert-coin voice prompt, coin drop, counting music, success,
error, click). A starter set of original, generated sounds ships in
`mikrotik/gui/sounds/` (about 380 KB total). Replace any of them with
your own `.mp3` using the exact filenames in that folder's `README.md`;
every one is optional and a missing file just means that cue stays
silent (no error).

Off by default: enable it from `admin.html`'s Settings → General →
**Enable Sounds** toggle (`AdminAPI::soundEnabled`, persisted in
`config.json`, exposed unauthenticated via `GET /api/branding` since
the customer sees these pages before any admin session exists — see
`gui_handler.h`'s `handleBranding()`). With it off, the speaker icon on
login/status never even renders (`script.js`'s `applyBranding()` hides
`#soundToggle` entirely), so a shop with no audio files sees nothing
sound-related at all.

With it on, each visitor still gets their own mute toggle (the speaker
icon, `SoundManager` in `script.js`) — the admin switch controls
whether the *feature* exists on the page, the per-visit toggle controls
whether *this browser* currently hears it. Background music can't
autoplay on page load (browser policy blocks `Audio.play()` without a
prior real click) — it starts the first time the visitor taps the
speaker icon or the "Insert Coin" prompt, both genuine click handlers.

## Adding a page
1. Copy an existing page as a starting point (keeps the `<link
   rel="stylesheet" href="style.css">` and `<script src="script.js">`
   tags).
2. Any new dynamic behavior belongs in `script.js`, calling the
   existing `/api/*` routes in `firmware/gui_handler.h` — don't
   duplicate business logic client-side (tier pricing, session
   expiry, etc. must stay authoritative on the NodeMCU, since the
   browser is not trusted).
3. If you need a new backend capability, add a handler + route to
   `firmware/gui_handler.h` (and the underlying logic to
   `session.h`/`admin_api.h`/etc.), then re-flash.

## Testing without a router
Run `python tools/mock_server.py` — it serves `mikrotik/gui/*` and
implements the same `/api/*` contract as `firmware/gui_handler.h`
in-memory (seeded with a few `TEST-*` voucher codes, admin password
`admin`), so you can click through login → status → admin in a real
browser with no MikroTik/NodeMCU involved. It's a dev-only stand-in —
state resets on restart and Night Promo's live clock isn't simulated —
but it's the fastest way to catch GUI/JS bugs before touching hardware.
For a true end-to-end test you still need it served from the MikroTik
with a real NodeMCU reachable, since captive-portal redirects depend on
that. (`tools/mock_server.py` does simulate the shape of MikroTik's own
`/login` redirect — see `07-features-guide.md`'s Dual Mode section — but
it isn't a real walled garden, so this is still a click-through
convenience, not proof the real thing works.)

## Automated contract check (no browser needed)
Run `python tools/regression_test.py` — it launches `mock_server.py` as
a subprocess and drives its full `/api/*` contract (login, pause/
resume/extend/disconnect, admin auth and role gating, settings,
voucher/subscriber/admin-account CRUD, the brute-force lockout) via
plain HTTP requests, then prints a pass/fail summary and exits non-zero
if anything regressed. Useful after touching `gui_handler.h` or
`mock_server.py` to catch a broken status code or a renamed/missing
response field before opening a browser at all. Same caveat as above —
it can't catch GUI-only bugs (a form field that isn't wired into
`script.js`), and it's not a substitute for real hardware.
