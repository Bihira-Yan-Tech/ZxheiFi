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
import tempfile
import time
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))
import zx_protocol as zx  # noqa: E402
from sub_sim import ChargeSim, SubSim  # noqa: E402

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
    conn = http.client.HTTPConnection("127.0.0.1", PORT, timeout=5)
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


# ---------------------------------------------------------------- Task 5

def test_pairing():
    code, _, _ = add_code("Tindahan")
    sub = SubSim(port=PORT)
    st, d, ok = sub.pair(code)
    check("pair -> 200", st == 200, f"{st} {d}")
    check("pair reply is signed with the code's key", ok)
    check("first sub is vendo 1", sub.vid == 1 and d.get("name") == "Tindahan", d)
    check("pair reply carries config + lastCoinSeq",
          d.get("config", {}).get("coinPin") == "D5" and d.get("lastCoinSeq") == 0, d)
    v = vendo(list_vendos(), 1)
    check("sub listed paired + online", v and v["paired"] and v["online"], v)
    check("code consumed", list_vendos().get("pending") == [], list_vendos())
    st, d, _ = SubSim(port=PORT, mac="AA:BB:CC:DD:EE:02").pair(code)
    check("same code again -> 401 bad_code", st == 401 and d.get("error") == "bad_code", f"{st} {d}")


def test_pair_wrong_code_rate_limit():
    add_code()
    sub = SubSim(port=PORT)
    codes = [sub.pair("2222-3333-4444")[0] for _ in range(5)]
    check("wrong codes -> 401", codes == [401] * 5, codes)
    st, d, _ = sub.pair("2222-3333-4444")
    check("6th wrong attempt -> 429", st == 429 and d.get("error") == "too_many_attempts", f"{st} {d}")
    advance(61)
    st, _, _ = sub.pair("2222-3333-4444")
    check("allowed again after a minute (still wrong -> 401)", st == 401, st)


def test_pair_expired_code():
    code, _, _ = add_code()
    advance(901)
    st, d, _ = SubSim(port=PORT).pair(code)
    check("expired code -> 401", st == 401, f"{st} {d}")


def test_poll_auth_and_replay():
    sub = paired_sub()
    st, d, ok = sub.poll()
    check("poll -> 200 signed", st == 200 and ok, f"{st} {d}")
    check("poll echoes n, relay off", d.get("n") == sub.n and d.get("relay") is False and d.get("rid") == 0, d)
    st, d, _ = sub.raw_post("/api/vendo/poll", {"v": sub.vid, "n": sub.next_n()}, sub.key, tamper=True)
    check("tampered signature -> 401 bad_sig", st == 401 and d.get("error") == "bad_sig", f"{st} {d}")
    st, d, _ = sub.raw_post("/api/vendo/poll", {"v": sub.vid, "n": 1}, sub.key)
    check("replayed counter -> 409 replay", st == 409 and d.get("error") == "replay", f"{st} {d}")
    st, d, _ = sub.raw_post("/api/vendo/poll", {"v": 3, "n": 99}, sub.key)
    check("unknown vendo -> 401 unpaired", st == 401 and d.get("error") == "unpaired", f"{st} {d}")
    advance(11)
    check("offline after 10 s of silence", vendo(list_vendos(), 1)["online"] is False)
    sub.poll()
    check("online again on the next poll", vendo(list_vendos(), 1)["online"] is True)


def test_coin_dedupe_and_box():
    sub = paired_sub()
    sub.drop_coin(10)
    res = sub.flush()
    check("coin -> ok", res and res[0][0] == 200 and res[0][1].get("ok") is True and res[0][2], res)
    st, d, ok = sub.send_coin({"seq": 1, "peso": 10, "rid": 0})
    check("resend of seq 1 -> dup, not credited twice", st == 200 and d.get("dup") is True and ok, f"{st} {d}")
    check("box counted once", vendo(list_vendos(), 1)["boxTotal"] == 10, vendo(list_vendos(), 1))
    st, d, _ = sub.send_coin({"seq": 2, "peso": 0, "rid": 0})
    check("zero-peso coin -> 400", st == 400 and d.get("error") == "bad_coin", f"{st} {d}")
    st, d, _ = sub.send_coin({"seq": 3, "peso": 50000, "rid": 0})
    check("absurd coin -> 400", st == 400, f"{st} {d}")


def test_vendo_limit():
    for i in range(3):
        paired_sub(f"Sub {i}", mac=f"AA:BB:CC:DD:EE:1{i}")
    ids = [v["id"] for v in list_vendos()["vendos"]]
    check("3 subs paired as 1..3", ids == [0, 1, 2, 3], ids)
    _, st, d = add_code("Fourth")
    check("4th sub -> 409 vendo_limit", st == 409 and d.get("error") == "vendo_limit" and d.get("limit") == 3,
          f"{st} {d}")


def test_config_push():
    sub = paired_sub()
    sub.poll()
    before = sub.cfg_ver
    st, d = admin_post("/api/admin/vendos/update",
                       {"id": 1, "coinPin": "D6", "relayPin": "none", "pesosPerPulse": 5, "relayActiveHigh": False})
    check("update sub pins -> 200", st == 200, f"{st} {d}")
    sub.poll()
    check("poll shows new cfgVer and sub fetched config",
          sub.cfg_ver == before + 1 and sub.config.get("coinPin") == "D6" and sub.config.get("pesosPerPulse") == 5
          and sub.config.get("relayPin") == "none" and sub.config.get("relayActiveHigh") is False, sub.config)
    st, d = admin_post("/api/admin/vendos/update", {"id": 1, "coinPin": "D3"})
    check("boot pin refused", st == 400 and d.get("error") == "invalid_pin", f"{st} {d}")
    st, d = admin_post("/api/admin/vendos/update", {"id": 1, "coinPin": "D7", "relayPin": "D7"})
    check("coin + relay same pin refused", st == 400 and d.get("error") == "coin_and_relay_same_pin", f"{st} {d}")
    st, d = admin_post("/api/admin/vendos/update", {"id": 1, "commissionPct": 20})
    check("commission change -> 200, no cfgVer bump", st == 200 and d.get("cfgVer") == before + 1, f"{st} {d}")


def test_remove_and_repair():
    sub = paired_sub()
    sub.drop_coin(10)
    sub.flush()
    st, d = admin_post("/api/admin/vendos/repair", {"id": 1})
    check("re-pair -> new code", st == 200 and CODE_RE.match(d.get("code", "")), f"{st} {d}")
    new_code = d.get("code", "")
    st, d, _ = sub.poll()
    check("old key rejected right away", st == 401, f"{st} {d}")
    fresh = SubSim(port=PORT, mac="AA:BB:CC:DD:EE:01")
    st, d, ok = fresh.pair(new_code)
    check("re-paired to the same id, seq carried over", st == 200 and fresh.vid == 1 and d.get("lastCoinSeq") == 1
          and fresh.seq == 1, f"{st} {d}")
    check("box kept across re-pair", vendo(list_vendos(), 1)["boxTotal"] == 10)
    st, d = admin_post("/api/admin/vendos/remove", {"id": 1})
    check("remove -> 200", st == 200, f"{st} {d}")
    st, d, _ = fresh.poll()
    check("removed vendo -> 401", st == 401 and d.get("error") == "unpaired", f"{st} {d}")


def test_offline_logged_once():
    sub = paired_sub()
    sub.poll()
    advance(301)
    request("GET", "/api/health")
    request("GET", "/api/health")
    st, logs = admin_get("/api/admin/logs")
    offline = [e for e in logs if e["type"] == "vendo_offline"]
    check("offline logged exactly once", len(offline) == 1, offline)
    sub.poll()
    st, logs = admin_get("/api/admin/logs")
    check("back online logged", any(e["type"] == "vendo_online" for e in logs), [e["type"] for e in logs])


# ---------------------------------------------------------------- Task 6

def start_coin(vid, mac="AA:00:00:00:00:01", session=""):
    return request("POST", "/api/coin/start", {"mac": mac, "vendo": vid, "session": session})


def coin_status(token):
    return request("GET", "/api/coin/status?token=" + token)[1]


def today_by_vendo():
    today = admin_get("/api/admin/sales")[1].get("today", {})
    return {e["id"]: e["peso"] for e in today.get("byVendo", [])}


def test_branding_lists_vendos():
    names = [v["name"] for v in request("GET", "/api/branding")[1].get("vendos", [])]
    check("branding: Main only", names == ["Main"], names)
    sub = paired_sub("Tindahan")
    sub.poll()
    vs = request("GET", "/api/branding")[1].get("vendos", [])
    check("branding: Main + Tindahan online", [(v["id"], v["name"], v["online"]) for v in vs] ==
          [(0, "Main", True), (1, "Tindahan", True)], vs)
    add_code("Not yet paired")
    check("unpaired codes not shown to customers", len(request("GET", "/api/branding")[1]["vendos"]) == 2)


def test_offline_sub_refused():
    sub = paired_sub()
    advance(11)
    st, d = start_coin(1)
    check("Insert Coin on an offline sub -> 409 vendo_offline", st == 409 and d.get("error") == "vendo_offline",
          f"{st} {d}")
    sub.poll()
    st, d = start_coin(1)
    check("online again -> 200", st == 200 and d.get("vendo") == 1, f"{st} {d}")


def test_two_boxes_at_once_and_sub_coin_flow():
    sub = paired_sub()
    sub.poll()
    st0, d0 = start_coin(0, "AA:00:00:00:00:01")
    st1, d1 = start_coin(1, "AA:00:00:00:00:02")
    check("two customers on two boxes at once", st0 == 200 and st1 == 200, f"{st0} {d0} / {st1} {d1}")
    sub.poll()
    check("sub relay on with a reservation id", sub.relay and sub.rid > 0, (sub.relay, sub.rid))
    sub.drop_coin(10)
    sub.flush()
    s = coin_status(d1["token"])
    check("sub coin credited to that customer", s.get("state") == "inserting" and s.get("pesos") == 10
          and s.get("vendo") == 1, s)
    check("main customer untouched", coin_status(d0["token"]).get("pesos") == 0)
    st, done = request("POST", "/api/coin/done", {"token": d1["token"]})
    check("done -> session", st == 200 and done.get("sessionId"), f"{st} {done}")
    check("sales split per vendo", today_by_vendo() == {1: 10}, today_by_vendo())
    sub.poll()
    check("relay off after done", sub.relay is False and sub.rid == 0, (sub.relay, sub.rid))


def test_late_coin_extends_the_payer():
    sub = paired_sub()
    sub.poll()
    st, d = start_coin(1)
    sub.poll()
    rid = sub.rid
    sub.drop_coin(10)
    sub.flush()
    _, done = request("POST", "/api/coin/done", {"token": d["token"]})
    before = request("GET", "/api/status?session=" + done["sessionId"])[1]["timeRemainingSec"]
    sub.drop_coin(10, rid=rid)   # went in before the relay closed, delivered late
    res = sub.flush()
    after = request("GET", "/api/status?session=" + done["sessionId"])[1]["timeRemainingSec"]
    check("late coin acked", res and res[-1][1].get("ok") is True, res)
    check("late coin added 60 min to the payer", 3590 <= after - before <= 3600, (before, after))
    check("late coin counted in sales", today_by_vendo() == {1: 20}, today_by_vendo())
    logs = [e["type"] for e in admin_get("/api/admin/logs")[1]]
    check("coin_late_extended logged", "coin_late_extended" in logs, logs)


def test_old_rid_never_credits_next_customer():
    sub = paired_sub()
    sub.poll()
    _, a = start_coin(1, "AA:00:00:00:00:0A")
    sub.poll()
    old_rid = sub.rid
    sub.drop_coin(10)
    sub.flush()
    request("POST", "/api/coin/done", {"token": a["token"]})
    advance(601)  # customer A's result is no longer held
    sub.poll()    # (and the sub keeps polling, so it's still online)
    _, b = start_coin(1, "AA:00:00:00:00:0B")
    sub.poll()
    sub.drop_coin(20, rid=old_rid)
    sub.flush()
    check("customer B not credited with A's stray coin", coin_status(b["token"]).get("pesos") == 0,
          coin_status(b["token"]))


def test_orphan_claimed_then_unclaimed():
    sub = paired_sub()
    sub.poll()
    sub.drop_coin(10, rid=0)
    sub.flush()
    _, d = start_coin(1)
    check("orphan coin claimed by the next Insert Coin", coin_status(d["token"]).get("pesos") == 10,
          coin_status(d["token"]))
    sub.drop_coin(20, rid=0)   # relay was off when it went in: not this customer's
    sub.flush()
    check("rid-0 coin not given to the open reservation", coin_status(d["token"]).get("pesos") == 10,
          coin_status(d["token"]))
    advance(61)                # the P10 customer auto-finishes (45 s), the P20 orphan expires (60 s)
    request("GET", "/api/health")
    check("sale + unclaimed orphan both counted", today_by_vendo().get(1) == 30, today_by_vendo())
    logs = [e["type"] for e in admin_get("/api/admin/logs")[1]]
    check("coin_late_unclaimed logged", "coin_late_unclaimed" in logs, logs)


def test_main_sales_by_vendo():
    _, d = start_coin(0)
    request("POST", "/dev/coin", {"peso": 20})
    request("POST", "/api/coin/done", {"token": d["token"]})
    check("main unit sales under vendo 0", today_by_vendo() == {0: 20}, today_by_vendo())


# ---------------------------------------------------------------- Charging (part 2)

def charging_box(name="Kanto Charging", mac="AA:BB:CC:DD:EE:C1"):
    st, d = admin_post("/api/admin/vendos/add", {"name": name, "type": "charging"})
    box = ChargeSim(port=PORT, mac=mac)
    box.pair(d.get("code", ""))
    return box


def get_settings():
    return admin_get("/api/admin/settings")[1]


def test_charging_pairing_and_config():
    st, d = admin_post("/api/admin/vendos/add", {"name": "X", "type": "toaster"})
    check("unknown vendo type -> 400 bad_type", st == 400 and d.get("error") == "bad_type", f"{st} {d}")
    box = charging_box()
    cfg = box.config
    check("charging box paired", box.vid == 1 and box.key is not None, box.last)
    check("config says charging with 4 ports", cfg.get("type") == "charging" and cfg.get("ports") == 4
          and cfg.get("portActiveHigh") is False, cfg)
    check("config carries rates, max and language",
          cfg.get("rates") == [{"peso": 5, "minutes": 30}, {"peso": 10, "minutes": 60}, {"peso": 20, "minutes": 150}]
          and cfg.get("maxMinutes") == 180 and cfg.get("lang") == "tl", cfg)
    v = vendo(list_vendos(), 1)
    check("list shows type + ports", v and v["type"] == "charging" and v["ports"] == 4, v)
    wifi = paired_sub("Tindahan", mac="AA:BB:CC:DD:EE:02")
    check("wifi box config says wifi", wifi.config.get("type") == "wifi" and "rates" not in wifi.config, wifi.config)


def test_charging_sale():
    box = charging_box()
    st, d, ok = box.charge(10, 2, 60)
    check("charge sale -> ok", st == 200 and d.get("ok") is True and ok, f"{st} {d}")
    today = admin_get("/api/admin/sales")[1]["today"]
    check("chargingRevenue counted", today.get("chargingRevenue") == 10, today)
    check("coinRevenue untouched", today.get("coinRevenue") == 0, today)
    check("byVendo has the charging box", today_by_vendo() == {1: 10}, today_by_vendo())
    check("box total counts it", vendo(list_vendos(), 1)["boxTotal"] == 10)
    st, d, ok = box.charge(10, 2, 60, seq=1)
    check("resent charge -> dup, counted once", d.get("dup") is True and today_by_vendo() == {1: 10}, f"{st} {d}")
    logs = [e for e in admin_get("/api/admin/logs")[1] if e["type"] == "charge"]
    check("charge logged with port + minutes", logs and "port 2" in logs[-1]["detail"] and "60 min" in logs[-1]["detail"], logs)
    box.charge(5, 0, 0)
    logs = [e["type"] for e in admin_get("/api/admin/logs")[1]]
    check("port-0 sale logged as charge_unclaimed", "charge_unclaimed" in logs, logs)
    st, d, _ = box.charge(0, 1, 30)
    check("zero peso -> 400", st == 400 and d.get("error") == "bad_charge", f"{st} {d}")
    st, d, _ = box.charge(10, 5, 60)
    check("port 5 -> 400", st == 400 and d.get("error") == "bad_charge", f"{st} {d}")
    admin_post("/api/admin/vendos/update", {"id": 1, "ports": 2})
    st, d, _ = box.charge(10, 4, 60)
    check("a port-4 sale queued before the box was cut to 2 ports is still accepted",
          st == 200 and d.get("ok") is True, f"{st} {d}")
    st, d, _ = box.charge(1000, 1, 7500)
    check("big fallback-priced sale accepted (never blocks the queue)", st == 200 and d.get("ok") is True, f"{st} {d}")


def test_charging_type_rules():
    box = charging_box()
    wifi = paired_sub("Tindahan", mac="AA:BB:CC:DD:EE:02")
    st, d, _ = box.send_coin({"seq": 1, "peso": 10, "rid": 0})
    check("coin from a charging box -> 400 wrong_type", st == 400 and d.get("error") == "wrong_type", f"{st} {d}")
    wifi.seq += 1
    n = wifi.next_n()
    st, d, _ = wifi.raw_post("/api/vendo/charge", {"v": wifi.vid, "n": n, "seq": 1, "peso": 10, "port": 1,
                                                  "minutes": 60}, wifi.key)
    check("charge from a wifi box -> 400 wrong_type", st == 400 and d.get("error") == "wrong_type", f"{st} {d}")
    box.poll()
    st, d = start_coin(1)
    check("Insert Coin on a charging box -> 404", st == 404 and d.get("error") == "vendo_unknown", f"{st} {d}")
    names = [v["name"] for v in request("GET", "/api/branding")[1]["vendos"]]
    check("branding lists wifi boxes only", names == ["Main", "Tindahan"], names)


def test_charging_stop():
    box = charging_box()
    box.charge(10, 2, 60)
    box.poll()
    st, d = admin_post("/api/admin/vendos/stop", {"id": 1, "port": 2}, STAFF)
    check("staff can Stop a port", st == 200, f"{st} {d}")
    box.poll()
    check("the box received and applied the Stop", box.stopped == [2] and box.ports[1] == 0, (box.stopped, box.ports))
    box.poll()
    check("acknowledged Stop is not sent again", box.stopped == [2], box.stopped)
    st, d = admin_post("/api/admin/vendos/stop", {"id": 1, "port": 5})
    check("Stop bad port -> 400", st == 400 and d.get("error") == "bad_port", f"{st} {d}")
    st, d = admin_post("/api/admin/vendos/stop", {"id": 0, "port": 1})
    check("Stop on a non-charging vendo -> 400", st == 400 and d.get("error") == "not_charging", f"{st} {d}")
    logs = [e["type"] for e in admin_get("/api/admin/logs")[1]]
    check("charge_stop logged", "charge_stop" in logs, logs)


def test_charging_port_status():
    box = charging_box()
    box.charge(10, 1, 60)
    box.poll()
    v = vendo(list_vendos(), 1)
    check("list shows port seconds from the poll", v.get("portSecs", [])[:2] == [3600, 0], v)


def test_charging_settings():
    s = get_settings()
    check("settings default charging rates",
          s.get("chargeRates") == [{"peso": 5, "minutes": 30}, {"peso": 10, "minutes": 60}, {"peso": 20, "minutes": 150}]
          and s.get("chargeMaxMinutes") == 180 and s.get("chargeLang") == "tl", s)
    box = charging_box()
    box.poll()
    before = box.cfg_ver
    st, d = admin_post("/api/admin/settings", {"chargeRates": [{"peso": 5, "minutes": 40}], "chargeMaxMinutes": 120,
                                               "chargeLang": "en"})
    check("save charging settings -> 200", st == 200, f"{st} {d}")
    box.poll()
    check("charging box refetched its config", box.cfg_ver != before and box.config.get("rates") == [
        {"peso": 5, "minutes": 40}] and box.config.get("maxMinutes") == 120 and box.config.get("lang") == "en",
          box.config)
    for body, err in (({"chargeRates": [{"peso": 5, "minutes": 30}, {"peso": 5, "minutes": 60}]}, "bad_charge_rates"),
                      ({"chargeRates": [{"peso": 0, "minutes": 30}]}, "bad_charge_rates"),
                      ({"chargeRates": [{"peso": 5, "minutes": 2000}]}, "bad_charge_rates"),
                      ({"chargeRates": [{"peso": i + 1, "minutes": 10} for i in range(11)]}, "bad_charge_rates"),
                      ({"chargeMaxMinutes": 5}, "bad_charge_max"),
                      ({"chargeLang": "jp"}, "bad_charge_lang")):
        st, d = admin_post("/api/admin/settings", body)
        check(f"invalid charging setting -> {err}", st == 400 and d.get("error") == err, f"{body} -> {st} {d}")


def test_charging_edit_and_limit():
    box = charging_box()
    st, d = admin_post("/api/admin/vendos/update", {"id": 1, "ports": 2, "portActiveHigh": True})
    check("edit ports + relay level -> 200", st == 200, f"{st} {d}")
    box.poll()
    check("box got ports=2", box.config.get("ports") == 2 and box.config.get("portActiveHigh") is True, box.config)
    st, d = admin_post("/api/admin/vendos/update", {"id": 1, "ports": 5})
    check("ports 5 -> 400", st == 400 and d.get("error") == "bad_ports", f"{st} {d}")
    st, d = admin_post("/api/admin/vendos/update", {"id": 1, "coinPin": "D1"})
    check("charging coin pin on the I2C bus -> 400", st == 400 and d.get("error") == "invalid_pin", f"{st} {d}")
    paired_sub("S1", mac="AA:BB:CC:DD:EE:02")
    paired_sub("S2", mac="AA:BB:CC:DD:EE:03")
    _, st, d = add_code("Fourth")
    check("charging + wifi boxes share the limit", st == 409 and d.get("error") == "vendo_limit", f"{st} {d}")


# ---------------------------------------------------------------- Backup / Restore (part 3)

def raw_request(method, path, body=b"", headers=None):
    conn = http.client.HTTPConnection("127.0.0.1", PORT, timeout=10)
    conn.request(method, path, body=body, headers=dict(headers or {}))
    resp = conn.getresponse()
    raw = resp.read()
    disposition = resp.getheader("Content-Disposition", "")
    conn.close()
    return resp.status, raw, disposition


def upload_backup(data: bytes, who=SUPER, field="backup"):
    boundary = "----zxheifitest7MA4YWxk"
    body = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"{field}\"; filename=\"b.json\"\r\n"
            f"Content-Type: application/json\r\n\r\n").encode() + data + f"\r\n--{boundary}--\r\n".encode()
    headers = dict(who, **{"Content-Type": f"multipart/form-data; boundary={boundary}"})
    status, raw, _ = raw_request("POST", "/api/admin/restore", body, headers)
    try:
        return status, json.loads(raw)
    except json.JSONDecodeError:
        return status, {}


def download_backup(who=SUPER):
    status, raw, disposition = raw_request("GET", "/api/admin/backup", headers=who)
    try:
        return status, json.loads(raw), raw, disposition
    except json.JSONDecodeError:
        return status, None, raw, disposition


def test_backup_download():
    paired_sub("Tindahan")
    st, data, raw, disp = download_backup()
    check("backup -> 200 JSON", st == 200 and isinstance(data, dict), f"{st} {raw[:200]}")
    check("offered as a file download", "attachment" in disp and "zxheifi-backup-" in disp, disp)
    check("backup header", data.get("zxheifiBackup") == 1 and data.get("board") and data.get("fw"), data)
    files = data.get("files", {})
    for name in ("config.json", "vouchers.json", "subscribers.json", "admins.json", "vendos.json", "sales.json"):
        check(f"backup has {name}", name in files, list(files))
    check("no network credentials in the backup", "network.json" not in files and b"apiPass" not in raw
          and b"wifiPassword" not in raw, list(files))
    vend = files.get("vendos.json", [])
    check("box keys included (no re-pair after restore)", any(v.get("key") for v in vend), vend)
    st, _, _, _ = download_backup(STAFF)
    check("staff cannot download a backup -> 403", st == 403, st)
    st, _, _, _ = download_backup({})
    check("no login -> 401", st == 401, st)


def test_restore_round_trip():
    paired_sub("Tindahan")
    _, backup, raw, _ = download_backup()
    # change things after the backup
    admin_post("/api/admin/settings", {"announcement": "CHANGED", "chargeMaxMinutes": 99})
    admin_post("/api/admin/vendos/update", {"id": 1, "name": "Renamed"})
    admin_post("/api/admin/vendos/remove", {"id": 1})
    st, d = upload_backup(raw)
    check("restore -> 200 ok", st == 200 and d.get("ok") is True and "config.json" in d.get("files", []), f"{st} {d}")
    s = get_settings()
    check("settings back", s.get("announcement") == "" and s.get("chargeMaxMinutes") == 180, s)
    v = vendo(list_vendos(), 1)
    check("removed box back, name back", v and v["name"] == "Tindahan", list_vendos())
    logs = [e["type"] for e in admin_get("/api/admin/logs")[1]]
    check("restore logged", "restore" in logs, logs)


def test_restore_rejects_bad_files():
    _, _, raw, _ = download_backup()
    before = get_settings().get("announcement")
    admin_post("/api/admin/settings", {"announcement": "KEEP ME"})
    st, d = upload_backup(b"this is not json at all")
    check("garbage -> 400 bad_backup", st == 400 and d.get("error") == "bad_backup", f"{st} {d}")
    st, d = upload_backup(b'{"hello": "world"}')
    check("JSON that isn't a backup -> 400 not_a_backup", st == 400 and d.get("error") == "not_a_backup", f"{st} {d}")
    st, d = upload_backup(raw[: len(raw) // 2])
    check("truncated backup -> 400 bad_backup", st == 400 and d.get("error") == "bad_backup", f"{st} {d}")
    st, d = upload_backup(b'{"zxheifiBackup":1,"files":{"network.json":{"x":1}}}')
    check("unknown file in a backup -> 400 bad_backup", st == 400 and d.get("error") == "bad_backup", f"{st} {d}")
    check("nothing changed by rejected restores", get_settings().get("announcement") == "KEEP ME", before)
    st, d = upload_backup(raw, field="other")
    check("no 'backup' field -> 400 no_file", st == 400 and d.get("error") == "no_file", f"{st} {d}")
    st, d = upload_backup(b"{" + b" " * (1024 * 1024 + 10) + b"}")
    check("over 1 MB -> 400 backup_too_large", st == 400 and d.get("error") == "backup_too_large", f"{st} {d}")
    st, d = upload_backup(raw, who=STAFF)
    check("staff cannot restore -> 403", st == 403, f"{st} {d}")


def test_board_pin_choices():
    h = request("GET", "/api/health")[1]
    check("health reports board + pin choices", h.get("board") == "esp8266" and
          h.get("pinChoices") == ["D1", "D2", "D5", "D6", "D7"], h)
    s = get_settings()
    check("settings report pin choices", s.get("pinChoices") == ["D1", "D2", "D5", "D6", "D7"], s)


TESTS = [
    test_backup_download,
    test_restore_round_trip,
    test_restore_rejects_bad_files,
    test_board_pin_choices,
    test_charging_pairing_and_config,
    test_charging_sale,
    test_charging_type_rules,
    test_charging_stop,
    test_charging_port_status,
    test_charging_settings,
    test_charging_edit_and_limit,
    test_branding_lists_vendos,
    test_offline_sub_refused,
    test_two_boxes_at_once_and_sub_coin_flow,
    test_late_coin_extends_the_payer,
    test_old_rid_never_credits_next_customer,
    test_orphan_claimed_then_unclaimed,
    test_main_sales_by_vendo,
    test_pairing,
    test_pair_wrong_code_rate_limit,
    test_pair_expired_code,
    test_poll_auth_and_replay,
    test_coin_dedupe_and_box,
    test_vendo_limit,
    test_config_push,
    test_remove_and_repair,
    test_offline_logged_once,
    test_clock_releases_unpaid_reservation,
    test_unknown_vendo_on_coin_start,
    test_default_vendo_list,
    test_add_vendo_codes,
    test_update_main,
    test_collect_main_box,
    test_cannot_remove_main,
]


# ---------------------------------------------------------------- runner

def wait_for_server(timeout_sec=40):   # a busy PC can take >10 s to start Python
    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        try:
            conn = http.client.HTTPConnection("127.0.0.1", PORT, timeout=1)
            conn.request("GET", "/api/health")
            conn.getresponse().read()
            conn.close()
            return True
        except OSError:
            time.sleep(0.1)
    return False


_run_count = [0]


def run_one(test):
    # A fresh port per test: on Windows the previous server's port can
    # still be held for a moment after it exits.
    global PORT
    PORT = 8100 + (_run_count[0] % 60)
    _run_count[0] += 1
    # Server errors go to a file, not a pipe: an unread pipe fills up after a
    # few tracebacks and freezes the server mid-test.
    log_path = Path(tempfile.gettempdir()) / f"zx_vendo_test_{PORT}.log"
    log = open(log_path, "w", encoding="utf-8")
    proc = subprocess.Popen([sys.executable, str(TOOLS_DIR / "mock_server.py"), str(PORT)],
                            stdout=subprocess.DEVNULL, stderr=log, text=True)
    try:
        if not wait_for_server():
            FAIL.append(f"{test.__name__}: mock server never came up (see {log_path})")
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
        log.close()
        errors = log_path.read_text(encoding="utf-8")
        if "Traceback" in errors:
            FAIL.append(f"{test.__name__}: mock server raised:\n{errors[-1500:]}")
            print(f"FAIL {test.__name__}: mock server raised\n{errors[-1500:]}", flush=True)


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
