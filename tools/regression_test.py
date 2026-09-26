#!/usr/bin/env python3
"""
regression_test.py - Automated contract check against mock_server.py's
/api/* routes (which mirror firmware/gui_handler.h).

Not a replacement for real-hardware testing or a browser click-through -
it can't catch GUI-only bugs (e.g. a form field that isn't wired into
script.js). What it DOES catch, in a few seconds with no browser or
MikroTik/NodeMCU involved: broken auth gating (401/403/429), wrong
status codes, missing/renamed response fields, and any backend logic
regression in the request/response contract itself.

Launches mock_server.py as a real subprocess on a throwaway port (the
same way it's normally run for manual testing) rather than importing
and threading it in-process - an in-process ThreadingHTTPServer sharing
the GIL with a synchronous test client turned out to deadlock on
certain requests, so a real subprocess is both simpler and matches how
this tool is actually used elsewhere in the project.

Usage:
    python tools/regression_test.py
Exit code 0 if every check passes, 1 otherwise.
"""
import http.client
import json
import subprocess
import sys
import time
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
PORT = 8099
LOGIN_MAX_FAILURES = 8  # mirrors mock_server.py's own constant
PASS = []
FAIL = []


def check(name, condition, detail=""):
    if not condition:
        print("FAIL " + name, flush=True)  # only failures print inline; see the summary for the full tally
    if condition:
        PASS.append(name)
    else:
        FAIL.append(f"{name} {('- ' + detail) if detail else ''}".strip())


def request(method, path, body=None, headers=None, is_json=True):
    conn = http.client.HTTPConnection("127.0.0.1", PORT, timeout=5)
    payload = None
    hdrs = dict(headers or {})
    if body is not None:
        payload = json.dumps(body).encode() if is_json else body
        hdrs.setdefault("Content-Type", "application/json" if is_json else "text/csv")
    conn.request(method, path, body=payload, headers=hdrs)
    resp = conn.getresponse()
    raw = resp.read()
    conn.close()
    try:
        data = json.loads(raw) if raw else {}
    except json.JSONDecodeError:
        data = {}
    return resp.status, data


def admin_headers(user, pw):
    return {"X-Admin-Username": user, "X-Admin-Password": pw}


def run():
    # ---- Public health/branding ---------------------------------------
    status, data = request("GET", "/api/health")
    check("GET /api/health -> 200", status == 200, f"got {status}")
    check("health has mikrotik+uptimeMs", "mikrotik" in data and "uptimeMs" in data)

    status, data = request("GET", "/api/branding")
    check("GET /api/branding -> 200", status == 200)
    check("branding has brandName+brandColor", "brandName" in data and "brandColor" in data)

    # ---- Voucher login/status/pause/resume/extend/disconnect ----------
    status, data = request("POST", "/api/login", {"code": "NOT-A-REAL-CODE", "mode": "hotspot"})
    check("invalid voucher -> 400", status == 400, f"got {status}")

    status, data = request("POST", "/api/login", {"code": "TEST-TIER1-0001", "mode": "hotspot"})
    check("valid voucher login -> 200", status == 200, f"got {status} {data}")
    check("login returns matching sessionId", data.get("sessionId") == "TEST-TIER1-0001")
    check("login returns tier 1", data.get("tier") == "1")
    check("login isSubscriber false", data.get("isSubscriber") is False)

    status, data = request("GET", "/api/status?session=TEST-TIER1-0001")
    check("status after login -> 200", status == 200)
    check("status not paused initially", data.get("paused") is False)

    status, data = request("POST", "/api/pause", {"session": "TEST-TIER1-0001"})
    check("pause -> 200", status == 200)
    status, data = request("GET", "/api/status?session=TEST-TIER1-0001")
    check("status reflects paused", data.get("paused") is True)

    status, data = request("POST", "/api/resume", {"session": "TEST-TIER1-0001", "password": ""})
    check("resume -> 200", status == 200)
    status, data = request("GET", "/api/status?session=TEST-TIER1-0001")
    check("status reflects resumed", data.get("paused") is False)

    # Reconnect: resubmitting an already-used-but-still-active voucher
    # code (e.g. a phone that randomized its MAC mid-session) must
    # succeed and resume the existing session, not be rejected as
    # invalid_or_used_voucher - see gui_handler.h's handleLogin().
    status, data = request("POST", "/api/login", {"code": "TEST-TIER1-0001", "mode": "hotspot"})
    check("reconnect with already-used-but-active code -> 200", status == 200, f"got {status} {data}")
    check("reconnect returns same sessionId", data.get("sessionId") == "TEST-TIER1-0001")
    check("reconnect returns same tier", data.get("tier") == "1")

    status, data = request("POST", "/api/login", {"code": "TEST-TIER2-0001", "mode": "hotspot"})
    check("second voucher login -> 200", status == 200)
    status, before = request("GET", "/api/status?session=TEST-TIER2-0001")
    status, data = request("POST", "/api/extend", {"session": "TEST-TIER2-0001", "code": "EXTEND-ME-00001"})
    check("extend -> 200", status == 200, f"got {status} {data}")
    check("extend increased time remaining", data.get("timeRemainingSec", 0) > before.get("timeRemainingSec", 0))

    status, data = request("POST", "/api/disconnect", {"session": "TEST-TIER1-0001"})
    check("disconnect -> 200", status == 200)
    status, data = request("GET", "/api/status?session=TEST-TIER1-0001")
    check("status after disconnect -> 404", status == 404, f"got {status}")

    # ---- Voucher codes are case-insensitive ----------------------------
    status, data = request("POST", "/api/login", {"code": "test-tier3-0001", "mode": "hotspot"})
    check("lower-case voucher code logs in", status == 200, f"got {status} {data}")
    check("lower-case login uses the canonical code", data.get("sessionId") == "TEST-TIER3-0001", f"got {data}")

    # ---- Coin session (firmware/coin_slot.h) ---------------------------
    status, data = request("POST", "/api/coin/start", {"mac": "AA:BB:CC:00:00:01"})
    check("coin start -> 200", status == 200, f"got {status} {data}")
    token = data.get("token", "")
    check("coin start returns a token", len(token) >= 8, f"got {data}")

    status, data = request("POST", "/api/coin/start", {"mac": "AA:BB:CC:00:00:02"})
    check("second phone gets coin_slot_busy", status == 409 and data.get("error") == "coin_slot_busy", f"got {status} {data}")
    check("busy reply says how long to wait", data.get("waitSec", 0) > 0, f"got {data}")

    status, data = request("POST", "/api/coin/done", {"token": token})
    check("Done with no coins -> no_coins_inserted", status == 400 and data.get("error") == "no_coins_inserted",
          f"got {status} {data}")

    status, data = request("POST", "/api/coin/start", {"mac": "AA:BB:CC:00:00:01"})
    token = data.get("token", "")
    request("POST", "/dev/coin", {"peso": 10})
    request("POST", "/dev/coin", {"peso": 10})
    request("POST", "/dev/coin", {"peso": 7})  # no Rate Profile for PHP7
    status, data = request("GET", "/api/coin/status?token=" + token)
    check("coin status is inserting", data.get("state") == "inserting", f"got {data}")
    check("two PHP10 coins credited additively", data.get("pesos") == 20 and data.get("minutes") == 120, f"got {data}")
    check("unknown denomination reported, not credited", data.get("unmatchedPesos") == 7, f"got {data}")

    status, data = request("GET", "/api/coin/status?token=WRONGTOKEN")
    check("another phone can't read this coin session", data.get("state") == "none", f"got {data}")

    status, data = request("POST", "/api/coin/done", {"token": token})
    check("coin done -> 200", status == 200, f"got {status} {data}")
    coin_code = data.get("sessionId", "")
    check("coin done returns a new code", coin_code.startswith("ZX") and data.get("extended") is False, f"got {data}")
    status, data = request("GET", "/api/status?session=" + coin_code)
    check("coin session is live with the paid time", status == 200 and data.get("timeRemainingSec", 0) > 7000,
          f"got {status} {data}")
    status, data = request("POST", "/api/coin/done", {"token": token})
    check("repeat Done returns the same code (no double credit)", data.get("sessionId") == coin_code, f"got {data}")
    status, data = request("GET", "/api/coin/status?token=" + token)
    check("status after done hands back the code", data.get("state") == "done" and data.get("code") == coin_code,
          f"got {data}")

    before_sec = request("GET", "/api/status?session=" + coin_code)[1].get("timeRemainingSec", 0)
    status, data = request("POST", "/api/coin/start", {"mac": "AA:BB:CC:00:00:01", "session": coin_code})
    token = data.get("token", "")
    request("POST", "/dev/coin", {"peso": 10})
    status, data = request("POST", "/api/coin/done", {"token": token})
    check("coin top-up extends the running session", status == 200 and data.get("extended") is True
          and data.get("sessionId") == coin_code, f"got {status} {data}")
    check("top-up added the coin's minutes", data.get("timeRemainingSec", 0) >= before_sec + 3500, f"got {data}")

    request("POST", "/dev/coin", {"peso": 20})  # dropped before tapping Insert Coin
    status, data = request("POST", "/api/coin/start", {"mac": "AA:BB:CC:00:00:03"})
    token = data.get("token", "")
    status, data = request("GET", "/api/coin/status?token=" + token)
    check("coin dropped just before tapping is credited", data.get("pesos") == 20, f"got {data}")
    status, data = request("POST", "/api/coin/cancel", {"token": token})
    status, data = request("GET", "/api/coin/status?token=" + token)
    check("cancel can't throw away paid credit", data.get("state") == "inserting", f"got {data}")
    request("POST", "/api/coin/done", {"token": token})

    request("POST", "/api/disconnect", {"session": "TEST-TIER2-0001"})  # cleanup, not asserted

    # ---- Admin kick/block: the real auth gap this project's CHR-testing
    # session found - /api/disconnect is the customer's own unauthenticated
    # self-service path by design, so the Active Users dashboard's Kick/
    # Block buttons must NOT go through it; they need their own
    # admin-authenticated endpoints instead.
    status, data = request("POST", "/api/login", {"code": "TEST-TIER3-0001", "mode": "hotspot"})
    check("kick/block test voucher login -> 200", status == 200, f"got {status} {data}")

    status, data = request("POST", "/api/admin/kick", {"session": "TEST-TIER3-0001"})
    check("admin kick with no creds -> 401", status == 401, f"got {status}")

    status, data = request("POST", "/api/admin/kick", {"session": "TEST-TIER3-0001"},
                            headers=admin_headers("admin", "admin"))
    check("admin kick with creds -> 200", status == 200, f"got {status} {data}")
    status, data = request("GET", "/api/status?session=TEST-TIER3-0001")
    check("session actually gone after admin kick", status == 404, f"got {status}")

    status, data = request("POST", "/api/admin/block", {"session": "EXTEND-ME-00001"},
                            headers=admin_headers("admin", "admin"))
    check("admin block -> 200", status == 200, f"got {status} {data}")
    check("block response includes a mac", "mac" in data, f"got {data}")
    status, data = request("GET", "/api/admin/blocked", headers=admin_headers("admin", "admin"))
    check("blocked list contains the new mac", any("EXTEND-ME-00001" in m for m in data), f"got {data}")

    status, data = request("POST", "/api/admin/unblock", {"mac": data[0] if data else ""},
                            headers=admin_headers("admin", "admin"))
    check("admin unblock -> 200", status == 200, f"got {status} {data}")

    status, data = request("POST", "/api/login", {"code": "TEST-EXPIRED-01", "mode": "hotspot"})
    check("expired voucher -> 400", status == 400, f"got {status}")

    # ---- Subscriber login -----------------------------------------------
    status, data = request("POST", "/api/login", {"code": "room5", "password": "tenant123", "mode": "hotspot"})
    check("subscriber login -> 200", status == 200, f"got {status} {data}")
    check("subscriber isSubscriber true", data.get("isSubscriber") is True)
    request("POST", "/api/disconnect", {"session": "room5"})  # cleanup

    status, data = request("POST", "/api/login", {"code": "room5", "password": "wrong-password", "mode": "hotspot"})
    check("subscriber wrong password rejected", status == 400, f"got {status}")

    # ---- Admin auth gating ------------------------------------------------
    status, data = request("GET", "/api/admin/overview")
    check("admin overview no creds -> 401", status == 401, f"got {status}")

    status, data = request("GET", "/api/admin/overview", headers=admin_headers("admin", "admin"))
    check("super admin overview -> 200", status == 200, f"got {status}")
    check("overview role is super", data.get("role") == "super")

    status, data = request("GET", "/api/admin/overview", headers=admin_headers("cashier1", "cashier123"))
    check("staff admin overview -> 200", status == 200)
    check("overview role is staff", data.get("role") == "staff")

    status, data = request("GET", "/api/admin/settings", headers=admin_headers("cashier1", "cashier123"))
    check("staff blocked from full settings -> 403", status == 403, f"got {status}")

    status, data = request("GET", "/api/admin/settings", headers=admin_headers("admin", "admin"))
    check("super can read settings -> 200", status == 200)

    status, data = request("GET", "/api/admin/tier-info", headers=admin_headers("cashier1", "cashier123"))
    check("staff CAN read tier-info -> 200", status == 200, f"got {status}")
    check("tier-info is the rate profiles array", isinstance(data, list) and len(data) > 0, f"got {data}")
    check("tier-info profile has pesoAmount", isinstance(data, list) and "pesoAmount" in data[0], f"got {data}")

    # ---- Rate Profiles export/import ---------------------------------------
    status, data = request("GET", "/api/admin/rate-profiles", headers=admin_headers("admin", "admin"))
    check("rate-profiles GET -> 200", status == 200)
    check("rate-profiles has a PHP10 entry", any(p["pesoAmount"] == 10 for p in data), f"got {data}")

    new_profiles = [{"pesoAmount": 10, "minutes": 90, "dataMb": 500, "validityMinutes": 1440, "speedProfile": "1"}]
    status, data = request("POST", "/api/admin/rate-profiles", new_profiles, headers=admin_headers("admin", "admin"))
    check("rate-profiles POST -> 200", status == 200, f"got {status} {data}")
    status, data = request("GET", "/api/admin/rate-profiles", headers=admin_headers("admin", "admin"))
    check("saved rate profile took effect", data == new_profiles, f"got {data}")
    # Restore the original 3-profile table so later checks in this run
    # (and a human re-reading Settings afterward) see the normal defaults.
    restore_profiles = [
        {"pesoAmount": 10, "minutes": 60, "dataMb": 500, "validityMinutes": 1440, "speedProfile": "1"},
        {"pesoAmount": 20, "minutes": 120, "dataMb": 1200, "validityMinutes": 1440, "speedProfile": "2"},
        {"pesoAmount": 30, "minutes": 180, "dataMb": 2000, "validityMinutes": 1440, "speedProfile": "3"},
    ]
    request("POST", "/api/admin/rate-profiles", restore_profiles, headers=admin_headers("admin", "admin"))

    # ---- Settings: speed profiles + rates saved together, announcement, pins ---
    H = admin_headers("admin", "admin")
    status, base = request("GET", "/api/admin/settings", headers=H)
    check("settings has speed profiles", isinstance(base.get("speedProfiles"), list) and len(base["speedProfiles"]) == 3,
          f"got {base.get('speedProfiles')}")
    speeds = base["speedProfiles"] + [{"id": "4", "name": "Piso 2M", "downMbps": 2, "upMbps": 1}]
    rates = restore_profiles + [{"pesoAmount": 5, "minutes": 1440, "dataMb": 0, "validityMinutes": 4320, "speedProfile": "4"}]
    status, data = request("POST", "/api/admin/settings",
                           {"speedProfiles": speeds, "rateProfiles": rates, "announcement": "Promo ngayong weekend!",
                            "coinPin": "D6", "relayPin": "D1", "relayActiveHigh": False, "coinPulseValue": 1}, headers=H)
    check("new speed + a rate using it saved in one go -> 200", status == 200, f"got {status} {data}")
    check("settings save reports mikrotikPushed", "mikrotikPushed" in data)
    status, data = request("GET", "/api/admin/settings", headers=H)
    check("speed profile 'Piso 2M' kept", any(sp["name"] == "Piso 2M" for sp in data.get("speedProfiles", [])), f"got {data}")
    check("coin pins kept", data.get("coinPin") == "D6" and data.get("relayPin") == "D1" and data.get("relayActiveHigh") is False,
          f"got {data}")
    status, data = request("GET", "/api/branding")
    check("portal gets the announcement", data.get("announcement") == "Promo ngayong weekend!", f"got {data}")
    check("portal gets the rate list", any(r["peso"] == 5 and r["minutes"] == 1440 and r["validityMinutes"] == 4320
                                           for r in data.get("rates", [])), f"got {data.get('rates')}")
    status, data = request("POST", "/api/admin/settings", {"speedProfiles": base["speedProfiles"]}, headers=H)
    check("removing a speed a rate still uses -> 400 speed_profile_in_use",
          status == 400 and data.get("error") == "speed_profile_in_use", f"got {status} {data}")
    status, data = request("POST", "/api/admin/settings", {"coinPin": "D5", "relayPin": "D5"}, headers=H)
    check("coin and relay on the same pin -> 400", status == 400 and data.get("error") == "coin_and_relay_same_pin",
          f"got {status} {data}")
    status, data = request("POST", "/api/admin/settings", {"coinPin": "D3"}, headers=H)
    check("boot pin D3 refused for the coin -> 400", status == 400 and data.get("error") == "invalid_pin", f"got {status} {data}")
    # back to defaults (rates first drop the 'Piso 2M' reference in the same save)
    status, data = request("POST", "/api/admin/settings",
                           {"speedProfiles": base["speedProfiles"], "rateProfiles": restore_profiles, "announcement": "",
                            "coinPin": "D5", "relayPin": "D7", "relayActiveHigh": True}, headers=H)
    check("restore defaults -> 200", status == 200, f"got {status} {data}")

    # ---- Voucher generate/import -------------------------------------------
    status, data = request("POST", "/api/admin/vouchers/generate", {"pesoAmount": 10, "count": 2}, headers=admin_headers("admin", "admin"))
    check("generate vouchers -> 200", status == 200)
    check("generate returns 2 codes", isinstance(data, list) and len(data) == 2, f"got {data}")
    if isinstance(data, list) and data:
        status, login_data = request("POST", "/api/login", {"code": data[0], "mode": "hotspot"})
        check("generated voucher actually logs in", status == 200, f"got {status} {login_data}")
        request("POST", "/api/disconnect", {"session": data[0]})

    status, data = request("POST", "/api/admin/vouchers/generate", {"pesoAmount": 999, "count": 1}, headers=admin_headers("admin", "admin"))
    check("generate with no matching profile -> 400", status == 400, f"got {status} {data}")

    csv_body = "code,tier,label,price_php,time_seconds,data_bytes,generated_at,used\nCSV-TEST-0001,1,Tier1,10,3600,524288000,2026-01-01,0\n"
    status, data = request("POST", "/api/admin/vouchers/import", csv_body, headers=admin_headers("admin", "admin"), is_json=False)
    check("CSV import -> 200", status == 200, f"got {status} {data}")
    check("CSV import counted 1", data.get("imported") == 1, f"got {data}")

    # ---- Subscribers CRUD -------------------------------------------------
    status, data = request("POST", "/api/admin/subscribers",
                            {"username": "regtest1", "password": "pw12345", "tier": "1", "days": 10, "price": 100},
                            headers=admin_headers("admin", "admin"))
    check("add subscriber -> 200", status == 200, f"got {status} {data}")
    status, data = request("POST", "/api/admin/subscribers",
                            {"username": "regtest2", "password": "pw12345", "tier": "99", "days": 10},
                            headers=admin_headers("admin", "admin"))
    check("subscriber on an unknown speed -> 400", status == 400 and data.get("error") == "unknown_speed_profile",
          f"got {status} {data}")
    status, data = request("GET", "/api/admin/sales", headers=admin_headers("admin", "admin"))
    check("subscriber payment shows in today's sales", data.get("today", {}).get("subscriptionRevenue", 0) >= 100,
          f"got {data.get('today')}")

    status, data = request("POST", "/api/login", {"code": "regtest1", "password": "pw12345", "mode": "hotspot"})
    check("new subscriber can log in", status == 200, f"got {status} {data}")
    request("POST", "/api/disconnect", {"session": "regtest1"})

    status, data = request("POST", "/api/admin/subscribers/renew", {"username": "regtest1", "days": 5, "price": 50},
                            headers=admin_headers("admin", "admin"))
    check("renew subscriber -> 200", status == 200)

    status, data = request("POST", "/api/admin/subscribers/toggle", {"username": "regtest1", "active": False},
                            headers=admin_headers("admin", "admin"))
    check("disable subscriber -> 200", status == 200)
    status, data = request("POST", "/api/login", {"code": "regtest1", "password": "pw12345", "mode": "hotspot"})
    check("disabled subscriber login rejected", status == 400, f"got {status}")

    # ---- Admin accounts -----------------------------------------------------
    status, data = request("POST", "/api/admin/accounts", {"username": "regstaff1", "password": "pw12345", "role": "staff"},
                            headers=admin_headers("admin", "admin"))
    check("add admin account -> 200", status == 200, f"got {status} {data}")
    status, data = request("POST", "/api/admin/accounts/toggle", {"username": "regstaff1", "active": False},
                            headers=admin_headers("admin", "admin"))
    check("toggle admin account -> 200", status == 200)

    # ---- Logs / Sales ----------------------------------------------------
    status, data = request("GET", "/api/admin/logs", headers=admin_headers("admin", "admin"))
    check("logs -> 200", status == 200)
    check("logs is a list", isinstance(data, list))

    status, data = request("GET", "/api/admin/sales", headers=admin_headers("admin", "admin"))
    check("sales -> 200", status == 200)
    check("sales has today+history", "today" in data and "history" in data)

    # ---- Brute-force lockout (MUST run last - it deliberately blocks
    # this IP for the remainder of the process, shared across every
    # endpoint that calls _require_admin()/checks /api/login) -----------
    last_status = None
    for _ in range(LOGIN_MAX_FAILURES):
        last_status, _ = request("POST", "/api/login", {"code": "STILL-NOT-REAL", "mode": "hotspot"})
    check(f"{LOGIN_MAX_FAILURES} failures still return 400 (not locked yet)", last_status == 400, f"got {last_status}")

    status, data = request("POST", "/api/login", {"code": "STILL-NOT-REAL", "mode": "hotspot"})
    check("attempt past the limit -> 429", status == 429, f"got {status}")

    status, data = request("POST", "/api/login", {"code": "TEST-NIGHT-0001", "mode": "hotspot"})
    check("even a valid code is blocked while locked out", status == 429, f"got {status}")

    status, data = request("GET", "/api/admin/overview", headers=admin_headers("admin", "admin"))
    check("admin auth also blocked (shared IP lockout table)", status == 429, f"got {status}")


def wait_for_server(timeout_sec=10):
    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        try:
            conn = http.client.HTTPConnection("127.0.0.1", PORT, timeout=1)
            conn.request("GET", "/api/health")
            conn.getresponse()
            conn.close()
            return True
        except (ConnectionRefusedError, OSError):
            time.sleep(0.2)
    return False


def main():
    proc = subprocess.Popen(
        [sys.executable, str(TOOLS_DIR / "mock_server.py"), str(PORT)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    try:
        if not wait_for_server():
            print("Mock server never came up:")
            print(proc.stdout.read())
            sys.exit(1)
        run()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    print(f"\n{len(PASS)} passed, {len(FAIL)} failed\n")
    if FAIL:
        print("FAILURES:")
        for f in FAIL:
            print("  -", f)
        sys.exit(1)
    print("All checks passed.")
    sys.exit(0)


if __name__ == "__main__":
    main()
