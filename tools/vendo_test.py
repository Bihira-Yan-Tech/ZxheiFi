#!/usr/bin/env python3
"""
vendo_test.py - contract tests for the v2 multi-vendo API (sub vendos,
pairing, signed protocol, per-vendo coins and sales) against
tools/mock_server.py, which mirrors firmware/vendo_api.h + coin_slot.h.

Each test gets a FRESH mock server (state never leaks between tests).
Timeouts are exercised with the mock's test clock (POST /dev/clock), so
nothing here sleeps for real.

Usage:  python tools/vendo_test.py [test_name ...]
Exit code 0 when every check passes.
"""
import http.client
import json
import re
import subprocess
import sys
import time
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))
import zx_protocol as zx  # noqa: E402
from sub_sim import SubSim  # noqa: E402

PORT = 8098
PASS, FAIL = [], []
CODE_RE = re.compile(r"^[2-9A-HJ-NP-Z]{4}-[2-9A-HJ-NP-Z]{4}-[2-9A-HJ-NP-Z]{4}$")


def check(name, condition, detail=""):
    if condition:
        PASS.append(name)
    else:
        print("FAIL " + name + (f" - {detail}" if detail else ""), flush=True)
        FAIL.append(f"{name} {('- ' + str(detail)) if detail else ''}".strip())


def request(method, path, body=None, headers=None):
    conn = http.client.HTTPConnection("localhost", PORT, timeout=5)
    hdrs = dict(headers or {})
    payload = None
    if body is not None:
        payload = json.dumps(body).encode()
        hdrs.setdefault("Content-Type", "application/json")
    conn.request(method, path, body=payload, headers=hdrs)
    resp = conn.getresponse()
    raw = resp.read()
    conn.close()
    try:
        data = json.loads(raw) if raw else {}
    except json.JSONDecodeError:
        data = {}
    return resp.status, data


SUPER = {"X-Admin-Username": "admin", "X-Admin-Password": "admin"}
STAFF = {"X-Admin-Username": "cashier1", "X-Admin-Password": "cashier123"}


def admin_get(path, who=SUPER):
    return request("GET", path, headers=who)


def admin_post(path, body, who=SUPER):
    return request("POST", path, body, headers=who)


def advance(sec):
    request("POST", "/dev/clock", {"advance": sec})


def list_vendos(who=SUPER):
    return admin_get("/api/admin/vendos", who)[1]


def vendo(data, vid):
    return next((v for v in data.get("vendos", []) if v["id"] == vid), None)


def add_code(name="Tindahan"):
    status, data = admin_post("/api/admin/vendos/add", {"name": name})
    return data.get("code", ""), status, data


def paired_sub(name="Tindahan", mac="AA:BB:CC:DD:EE:01"):
    code, _, _ = add_code(name)
    sub = SubSim(port=PORT, mac=mac)
    sub.pair(code)
    return sub


# ---------------------------------------------------------------- Task 3

def test_clock_releases_unpaid_reservation():
    st, d = request("POST", "/api/coin/start", {"mac": "AA:00:00:00:00:01"})
    check("start -> 200", st == 200, f"{st} {d}")
    st, d2 = request("POST", "/api/coin/start", {"mac": "AA:00:00:00:00:02"})
    check("second customer busy", st == 409 and d2.get("error") == "coin_slot_busy", f"{st} {d2}")
    advance(61)
    st, d3 = request("POST", "/api/coin/start", {"mac": "AA:00:00:00:00:02"})
    check("slot free after the 60 s no-coin timeout", st == 200, f"{st} {d3}")


def test_unknown_vendo_on_coin_start():
    st, d = request("POST", "/api/coin/start", {"mac": "AA:00:00:00:00:01", "vendo": 9})
    check("coin/start unknown vendo -> 404", st == 404 and d.get("error") == "vendo_unknown", f"{st} {d}")


TESTS = [
    test_clock_releases_unpaid_reservation,
    test_unknown_vendo_on_coin_start,
]


# ---------------------------------------------------------------- runner

def wait_for_server(timeout_sec=10):
    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        try:
            conn = http.client.HTTPConnection("localhost", PORT, timeout=1)
            conn.request("GET", "/api/health")
            conn.getresponse().read()
            conn.close()
            return True
        except OSError:
            time.sleep(0.1)
    return False


def run_one(test):
    proc = subprocess.Popen([sys.executable, str(TOOLS_DIR / "mock_server.py"), str(PORT)],
                            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    try:
        if not wait_for_server():
            FAIL.append(f"{test.__name__}: mock server never came up: {proc.stderr.read() if proc.poll() is not None else ''}")
            return
        before = len(FAIL)
        try:
            test()
        except Exception as exc:  # a crash is a failure, not a stop
            FAIL.append(f"{test.__name__}: {type(exc).__name__}: {exc}")
            print(f"FAIL {test.__name__}: {type(exc).__name__}: {exc}", flush=True)
        print(("ok   " if len(FAIL) == before else "BAD  ") + test.__name__, flush=True)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


def main():
    wanted = set(sys.argv[1:])
    for test in TESTS:
        if not wanted or test.__name__ in wanted:
            run_one(test)
    print(f"\n{len(PASS)} passed, {len(FAIL)} failed\n")
    if FAIL:
        print("FAILURES:")
        for f in FAIL:
            print("  -", f)
        sys.exit(1)
    print("All vendo checks passed.")


if __name__ == "__main__":
    main()
