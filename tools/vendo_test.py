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


# ---------------------------------------------------------------- Task 4

def test_default_vendo_list():
    st, d = admin_get("/api/admin/vendos")
    check("list -> 200", st == 200, f"{st} {d}")
    check("board esp8266, limit 3", d.get("board") == "esp8266" and d.get("limit") == 3, d)
    check("only Main", [v["id"] for v in d.get("vendos", [])] == [0], d)
    main = vendo(d, 0) or {}
    check("Main online + paired", main.get("online") is True and main.get("paired") is True, main)
    check("Main starts empty", main.get("boxTotal") == 0 and main.get("commissionPct") == 0, main)
    st, _ = request("GET", "/api/admin/vendos")
    check("list needs login -> 401", st == 401, st)


def test_add_vendo_codes():
    st, d = admin_post("/api/admin/vendos/add", {"name": "Tindahan"}, STAFF)
    check("staff cannot add -> 403", st == 403, f"{st} {d}")
    code, st, d = add_code("Tindahan")
    check("super add -> 200", st == 200, f"{st} {d}")
    check("pairing code format", bool(CODE_RE.match(code)), code)
    check("code valid 15 min", d.get("expiresInSec") == 900, d)
    pend = list_vendos().get("pending", [])
    check("super sees the pending code", len(pend) == 1 and pend[0]["code"] == code and pend[0]["name"] == "Tindahan", pend)
    check("staff does not see codes", list_vendos(STAFF).get("pending") == [], list_vendos(STAFF))
    add_code("Kanto")
    _, st, d = add_code("Third")
    check("3rd pending -> 409 too_many_pending", st == 409 and d.get("error") == "too_many_pending", f"{st} {d}")
    st, d = admin_post("/api/admin/vendos/add", {"name": "   "})
    check("blank name -> 400 bad_name", st == 400 and d.get("error") == "bad_name", f"{st} {d}")
    advance(901)
    check("codes expire after 15 min", list_vendos().get("pending") == [], list_vendos())


def test_update_main():
    st, d = admin_post("/api/admin/vendos/update", {"id": 0, "name": "Front Desk", "commissionPct": 10})
    check("update Main -> 200", st == 200, f"{st} {d}")
    main = vendo(list_vendos(), 0)
    check("Main renamed + commission", main["name"] == "Front Desk" and main["commissionPct"] == 10, main)
    st, d = admin_post("/api/admin/vendos/update", {"id": 0, "commissionPct": 101})
    check("commission 101 -> 400", st == 400 and d.get("error") == "bad_commission", f"{st} {d}")
    st, d = admin_post("/api/admin/vendos/update", {"id": 0, "name": ""})
    check("empty name -> 400", st == 400 and d.get("error") == "bad_name", f"{st} {d}")
    check("failed update changed nothing", vendo(list_vendos(), 0)["commissionPct"] == 10)
    st, d = admin_post("/api/admin/vendos/update", {"id": 0, "commissionPct": 5}, STAFF)
    check("staff cannot update -> 403", st == 403, st)
    st, d = admin_post("/api/admin/vendos/update", {"id": 7, "name": "X"})
    check("update unknown vendo -> 404", st == 404 and d.get("error") == "vendo_unknown", f"{st} {d}")


def test_collect_main_box():
    request("POST", "/dev/coin", {"peso": 10})  # no one reserved - orphan, but the cash is in the box
    check("box counts the coin", vendo(list_vendos(), 0)["boxTotal"] == 10, list_vendos())
    st, d = admin_post("/api/admin/vendos/collected", {"id": 0}, STAFF)
    check("staff can mark Collected -> 200", st == 200 and d.get("collected") == 10, f"{st} {d}")
    check("box back to 0", vendo(list_vendos(), 0)["boxTotal"] == 0)
    st, rows = admin_get("/api/admin/vendos/collections", STAFF)
    check("collection recorded", st == 200 and rows and rows[0]["amount"] == 10 and rows[0]["admin"] == "cashier1"
          and rows[0]["vendoId"] == 0, f"{st} {rows}")


def test_cannot_remove_main():
    st, d = admin_post("/api/admin/vendos/remove", {"id": 0})
    check("remove Main -> 400", st == 400 and d.get("error") == "cannot_remove_main", f"{st} {d}")
    st, d = admin_post("/api/admin/vendos/repair", {"id": 0})
    check("re-pair Main -> 400", st == 400 and d.get("error") == "cannot_repair_main", f"{st} {d}")


TESTS = [
    test_clock_releases_unpaid_reservation,
    test_unknown_vendo_on_coin_start,
    test_default_vendo_list,
    test_add_vendo_codes,
    test_update_main,
    test_collect_main_box,
    test_cannot_remove_main,
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
