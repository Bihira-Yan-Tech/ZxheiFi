#!/usr/bin/env python3
"""
generator.py - ZxheiFi voucher generator.

Creates voucher codes from the shared Rate Profiles table (the SAME
table the admin dashboard's Settings > Rate Profiles edits and can
Export/Import as JSON - see rate_profiles.json in this directory),
writes a CSV ledger (consumed by the admin dashboard / NodeMCU voucher
import), and renders printable thermal-receipt tickets (with embedded
QR codes) using print_template.html.

Requires: pip install qrcode pillow

Usage:
    python generator.py --profile 10 --count 20
    python generator.py --profile 5 --count 10 --outdir generated
    python generator.py --profile 20 --count 5 --format csv
    python generator.py --profile 10 --count 20 --rates rate_profiles.json
"""
import argparse
import base64
import csv
import io
import json
import random
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

try:
    import qrcode
except ImportError:
    sys.exit("Missing dependency 'qrcode'. Install with: pip install qrcode pillow")

# Used only if --rates points at a missing file, so this script still
# runs standalone without the admin dashboard's export - matches
# firmware/admin_api.h's seedDefaultRateProfiles() migration defaults.
DEFAULT_RATE_PROFILES = [
    {"pesoAmount": 10, "minutes": 60, "dataMb": 500, "validityMinutes": 1440, "speedProfile": "1"},
    {"pesoAmount": 20, "minutes": 120, "dataMb": 1200, "validityMinutes": 1440, "speedProfile": "2"},
    {"pesoAmount": 30, "minutes": 180, "dataMb": 2000, "validityMinutes": 1440, "speedProfile": "3"},
]

# Characters chosen to avoid visual ambiguity on printed receipts (no 0/O, 1/I/L).
CODE_ALPHABET = "23456789ABCDEFGHJKMNPQRSTUVWXYZ"


def load_rate_profiles(path: Path) -> list:
    if not path.exists():
        print(f"Note: {path.name} not found, using built-in defaults. "
              f"Export your real rates from the admin dashboard's Settings tab and save them here.")
        return DEFAULT_RATE_PROFILES
    try:
        profiles = json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError) as e:
        sys.exit(f"Could not read {path}: {e}")
    if not isinstance(profiles, list) or not profiles:
        sys.exit(f"{path} does not contain a non-empty JSON array of rate profiles")
    return profiles


def generate_code(prefix: str, group_size: int = 5, groups: int = 2) -> str:
    body = "".join(
        "-".join(
            "".join(random.choice(CODE_ALPHABET) for _ in range(group_size))
            for _ in range(groups)
        )
    )
    return f"{prefix}-{body}"


def format_data(num_bytes: int) -> str:
    if num_bytes <= 0:
        return "Unlimited"
    gb = num_bytes / (1024 ** 3)
    if gb >= 1:
        return f"{gb:.2f}GB"
    return f"{num_bytes / (1024 ** 2):.0f}MB"


def format_time(seconds: int) -> str:
    hours = seconds // 3600
    minutes = (seconds % 3600) // 60
    if minutes:
        return f"{hours}h{minutes}m"
    return f"{hours}hr"


def make_qr_data_uri(payload: str) -> str:
    img = qrcode.make(payload, border=2)
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    encoded = base64.b64encode(buf.getvalue()).decode("ascii")
    return f"data:image/png;base64,{encoded}"


def render_ticket(code: str, peso_amount: int, time_seconds: int, data_bytes: int, generated_at: str,
                   expiry_date=None, brand_name="ZXHEIFI", footer_text="Scan QR or type code to connect") -> str:
    qr_uri = make_qr_data_uri(code)
    validity_line = (
        f'<div class="meta">Valid until<span>{expiry_date}</span></div>' if expiry_date else ""
    )
    return f"""<div class="ticket">
    <div class="brand">{brand_name}</div>
    <div class="tier">&#8369;{peso_amount}</div>
    <div class="qr"><img src="{qr_uri}" alt="QR"></div>
    <div class="code">{code}</div>
    <div class="meta">Time<span>{format_time(time_seconds)}</span></div>
    <div class="meta">Data<span>{format_data(data_bytes)}</span></div>
    <div class="meta">Price<span>&#8369;{peso_amount}</span></div>
    {validity_line}
    <div class="footer">{generated_at} &middot; {footer_text}</div>
</div>"""


def write_csv(path: Path, rows: list, append: bool) -> None:
    is_new = not path.exists()
    mode = "a" if append and not is_new else "w"
    with path.open(mode, newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(
            f, fieldnames=["code", "tier", "label", "price_php", "time_seconds", "data_bytes",
                           "generated_at", "used", "expiry_epoch", "pause_window_min"]
        )
        if mode == "w" or is_new:
            writer.writeheader()
        writer.writerows(rows)


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate ZxheiFi vouchers.")
    parser.add_argument("--profile", required=True, type=int,
                         help="Peso amount of the Rate Profile to generate vouchers for, e.g. 10")
    parser.add_argument("--rates", default="rate_profiles.json",
                         help="Path to the shared Rate Profiles JSON (relative to vouchers/) - "
                              "export this from the admin dashboard's Settings tab")
    parser.add_argument("--count", type=int, default=1, help="Number of vouchers to generate")
    parser.add_argument("--outdir", default="generated", help="Output directory (relative to vouchers/)")
    parser.add_argument("--format", choices=["csv", "html", "both"], default="both")
    parser.add_argument("--prefix", default="ZX", help="Voucher code prefix")
    parser.add_argument("--brand-name", default="ZXHEIFI",
                         help="Business name printed on the ticket")
    parser.add_argument("--accent-color", default="#2dd4c9",
                         help="Hex color for the ticket's brand name/code text (mainly affects on-screen "
                              "preview and color printers - most thermal printers are monochrome)")
    parser.add_argument("--footer-text", default="Scan QR or type code to connect",
                         help="Small text line at the bottom of each ticket")
    args = parser.parse_args()

    if args.count < 1:
        sys.exit("--count must be at least 1")
    if not re.fullmatch(r"#[0-9a-fA-F]{6}", args.accent_color):
        sys.exit("--accent-color must be a 6-digit hex color, e.g. #2dd4c9")

    profiles = load_rate_profiles(Path(__file__).parent / args.rates)
    profile = next((p for p in profiles if int(p["pesoAmount"]) == args.profile), None)
    if not profile:
        available = ", ".join(str(p["pesoAmount"]) for p in profiles)
        sys.exit(f"No rate profile for peso amount {args.profile}. Available: {available}")

    time_seconds = int(profile["minutes"]) * 60
    data_mb = int(profile.get("dataMb", 0))
    data_bytes = data_mb * 1024 * 1024 if data_mb > 0 else 0  # 0 = unlimited, matches UNLIMITED_BYTES on-device
    validity_minutes = int(profile.get("validityMinutes", 0))
    speed_profile = str(profile.get("speedProfile", "1"))

    outdir = Path(__file__).parent / args.outdir
    outdir.mkdir(parents=True, exist_ok=True)

    now = datetime.now(timezone.utc)
    generated_at = now.strftime("%Y-%m-%d %H:%M UTC")
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")

    expiry_epoch = 0
    expiry_date_display = None
    if validity_minutes > 0:
        from datetime import timedelta
        expiry_dt = now + timedelta(minutes=validity_minutes)
        expiry_epoch = int(expiry_dt.timestamp())
        expiry_date_display = expiry_dt.strftime("%Y-%m-%d %H:%M")

    seen = set()
    rows = []
    tickets_html = []
    while len(rows) < args.count:
        code = generate_code(args.prefix)
        if code in seen:
            continue
        seen.add(code)
        rows.append({
            "code": code,
            "tier": speed_profile,
            "label": f"PHP{args.profile}",
            "price_php": args.profile,
            "time_seconds": time_seconds,
            "data_bytes": data_bytes,
            "generated_at": generated_at,
            "used": "no",
            "expiry_epoch": expiry_epoch,
            "pause_window_min": validity_minutes if validity_minutes > 0 else 120,
        })

    if args.format in ("csv", "both"):
        csv_path = outdir / f"vouchers-{args.profile}-{stamp}.csv"
        write_csv(csv_path, rows, append=False)
        print(f"CSV written: {csv_path} ({len(rows)} vouchers)")

    if args.format in ("html", "both"):
        for row in rows:
            tickets_html.append(render_ticket(
                row["code"], args.profile, time_seconds, data_bytes, generated_at, expiry_date_display,
                brand_name=args.brand_name, footer_text=args.footer_text,
            ))
        template_path = Path(__file__).parent / "print_template.html"
        template = template_path.read_text(encoding="utf-8")
        html = template.replace("{{TICKETS}}", "\n".join(tickets_html))
        html = html.replace("{{ACCENT_COLOR}}", args.accent_color)
        html = html.replace("ZxheiFi - Vouchers", f"{args.brand_name} - Vouchers")
        html_path = outdir / f"vouchers-{args.profile}-{stamp}.html"
        html_path.write_text(html, encoding="utf-8")
        print(f"Printable HTML written: {html_path} ({len(rows)} tickets)")
        print("Open it in a browser and use Print > Save as PDF, or print directly to a thermal printer.")


if __name__ == "__main__":
    main()
