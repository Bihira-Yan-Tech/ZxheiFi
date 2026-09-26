#!/usr/bin/env python3
"""
mock_server.py - Dev-only stand-in for the NodeMCU's JSON API.

Serves mikrotik/gui/*.html/css/js as static files AND implements the
same /api/* contract as firmware/gui_handler.h, so the GUI can be
clicked through in a real browser without any MikroTik/NodeMCU
hardware. Session state is in-memory only (resets on restart) and time
is compressed for faster manual testing.

Usage:
    python tools/mock_server.py [port]   # default port 8080

Not meant for production use - it is a test double for firmware
behavior, not a reimplementation of it.
"""
import json
import random
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs

GUI_DIR = Path(__file__).resolve().parent.parent / "mikrotik" / "gui"

# Mirrors firmware/config.h's UNLIMITED_SECONDS/UNLIMITED_BYTES sentinels.
UNLIMITED_SECONDS = 0xFFFFFFF0
UNLIMITED_BYTES = 0xFFFFFFFFFFF00000
MAX_PAUSE_MINUTES = 120
FIRMWARE_VERSION = "1.0.0"  # mirrors firmware/config.h

# Unified rate table - mirrors AdminAPI::rateProfiles. Every coin
# denomination AND every voucher price point draws from this SAME list;
# a peso amount with no matching entry gets no credit (see
# processCoinSlot()'s "no exact match -> log and discard" rule).
rate_profiles = [
    {"pesoAmount": 10, "minutes": 60, "dataMb": 500, "validityMinutes": 1440, "speedProfile": "1"},
    {"pesoAmount": 20, "minutes": 120, "dataMb": 1200, "validityMinutes": 1440, "speedProfile": "2"},
    {"pesoAmount": 30, "minutes": 180, "dataMb": 2000, "validityMinutes": 1440, "speedProfile": "3"},
]


def profile_by_peso(peso):
    return next((p for p in rate_profiles if p["pesoAmount"] == peso), None)


# username -> {password, role, active} - mirrors admin_api.h's AdminAccount.
# Matches config.h's default ADMIN_PASSWORD_HASH = MD5("admin").
admins = {
    "admin": {"password": "admin", "role": "super", "active": True},
    "cashier1": {"password": "cashier123", "role": "staff", "active": True},
}

# Seed a handful of known test vouchers so you can log in right away.
# expiryEpoch 0 = never expires, matching AdminAPI's VoucherRecord default.
# tier here is the resolved SPEED PROFILE ("1"/"2"/"3"), same field
# VoucherRecord.tier stores - not a peso amount.
vouchers = {
    "TEST-TIER1-0001": {"tier": "1", "used": False, "expiryEpoch": 0, "price": 10,
                         "timeSeconds": 3600, "dataBytes": 500 * 1024 * 1024, "pauseWindowMinutes": 1440},
    "TEST-TIER2-0001": {"tier": "2", "used": False, "expiryEpoch": 0, "price": 20,
                         "timeSeconds": 7200, "dataBytes": 1200 * 1024 * 1024, "pauseWindowMinutes": 1440},
    "TEST-TIER3-0001": {"tier": "3", "used": False, "expiryEpoch": 0, "price": 30,
                         "timeSeconds": 10800, "dataBytes": 2000 * 1024 * 1024, "pauseWindowMinutes": 1440},
    "EXTEND-ME-00001": {"tier": "1", "used": False, "expiryEpoch": 0, "price": 10,
                         "timeSeconds": 3600, "dataBytes": 500 * 1024 * 1024, "pauseWindowMinutes": 1440},
    "TEST-EXPIRED-01": {"tier": "1", "used": False, "expiryEpoch": int(time.time()) - 3600, "price": 10,
                         "timeSeconds": 3600, "dataBytes": 500 * 1024 * 1024, "pauseWindowMinutes": 1440},  # already expired, for testing
}
# username -> {password, tier, expiryEpoch, active} - mirrors Subscriber.
subscribers = {
    "room5": {"password": "tenant123", "tier": "2", "expiryEpoch": time.time() + 30 * 86400, "active": True},
}
sessions = {}  # sessionId -> {mode, tier, remainingSeconds, remainingBytes, lastTick, paused, pausedAt, pauseWindowMinutes, isSubscriber}
activity_log = []  # [{epoch, type, detail}], newest last (like the firmware's rolling log)
sales_history = []  # [{dateStamp, coinRevenue, voucherRevenue, subscriptionRevenue, users, dataUsedBytes}]
blocked_macs = []  # mirrors AdminAPI::blockedMacs - display/record only, no real MikroTik to enforce it here

admin_stats = {
    "usersToday": 0, "coinRevenueToday": 0.0, "voucherRevenueToday": 0.0,
    "subscriptionRevenueToday": 0.0, "dataUsedTodayBytes": 0,
    "coinByVendo": {},  # vendo id -> coin pesos today (mirrors AdminAPI::coinByVendoToday)
}
settings = {
    "nightPromoEnabled": True, "idleTimeoutMin": 5, "autoRebootTime": "03:00",
    "brandName": "ZXHEIFI", "brandColor": "#2dd4c9",
    "soundEnabled": False,
    # Mirrors AdminAPI: named speed profiles, announcement, coin-slot pins.
    "speedProfiles": [
        {"id": "1", "name": "Basic", "downMbps": 10.0, "upMbps": 5.0},
        {"id": "2", "name": "Gaming", "downMbps": 30.0, "upMbps": 10.0},
        {"id": "3", "name": "High", "downMbps": 50.0, "upMbps": 20.0},
    ],
    "announcement": "",
    "coinPin": "D5", "relayPin": "D7", "relayActiveHigh": True, "coinPulseValue": 1,
    "telegramEnabled": False, "telegramBotToken": "", "telegramChatId": "",
}


# Coin sessions - mirrors firmware/coin_slot.h: one reservation slot per
# vendo (0 = the main unit's own acceptor, 1..N = paired sub vendos). Real
# coins come from the acceptor's pulses; here POST /dev/coin
# {"peso": 10, "vendo": 0} drops one on the main unit's slot.
COIN_IDLE_TIMEOUT_SEC = 45
COIN_NO_COIN_TIMEOUT_SEC = 60
COIN_ORPHAN_WINDOW_SEC = 60
COIN_RESULT_KEEP_SEC = 600
CODE_ALPHABET = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ"

# Test clock: every coin/vendo timer reads mock_now(), which POST /dev/clock
# {"advance": seconds} pushes forward - timeout rules stay testable
# without waiting. (Session countdowns keep using the real clock.)
CLOCK = {"offset": 0.0}


def mock_now():
    return time.time() + CLOCK["offset"]


def new_slot():
    return {"reserved": False, "token": "", "mac": "", "extend": "", "last": 0.0, "rid": 0,
            "pesos": 0, "unmatched": 0, "seconds": 0, "bytes": 0, "unlimited": False,
            "tier": "1", "topPeso": 0, "pauseWindow": 0,
            "orphan": 0, "orphanAt": 0.0,
            "doneToken": "", "doneCode": "", "doneExtended": False, "doneAt": 0.0, "doneRid": 0}


slots = {0: new_slot()}
RID = {"next": random.randint(1, 1 << 20)}  # reservation ids, unique across slots and reboots


def coin_clear_credit(s):
    s.update(pesos=0, unmatched=0, seconds=0, bytes=0, unlimited=False, tier="1", topPeso=0, pauseWindow=0)


def coin_release(s):
    s.update(reserved=False, token="", rid=0)
    coin_clear_credit(s)


def coin_limit(s):
    return COIN_IDLE_TIMEOUT_SEC if s["seconds"] else COIN_NO_COIN_TIMEOUT_SEC


def coin_credit(s, peso):
    """Adds one coin's Rate Profile to a reservation; False if no profile matches."""
    p = profile_by_peso(peso)
    if not p:
        s["unmatched"] += peso
        log_event("coin_unmatched", f"PHP {peso} has no matching Rate Profile")
        return False
    s["pesos"] += peso
    s["seconds"] += p["minutes"] * 60
    if p["dataMb"] == 0:
        s["unlimited"] = True
    else:
        s["bytes"] += p["dataMb"] * 1024 * 1024
    if p["pesoAmount"] >= s["topPeso"]:
        s["topPeso"], s["tier"] = p["pesoAmount"], p["speedProfile"]
    s["pauseWindow"] = max(s["pauseWindow"], p.get("validityMinutes") or MAX_PAUSE_MINUTES)
    return True


def add_coin_revenue(vid, peso):
    admin_stats["coinRevenueToday"] += peso
    by = admin_stats["coinByVendo"]
    by[vid] = by.get(vid, 0) + peso


def coin_orphan(vid, peso):
    s = slots[vid]
    s["orphan"] += peso
    s["orphanAt"] = mock_now()
    log_event("coin_orphan", f"PHP {peso} inserted with no customer on Insert Coin" + (f" (vendo {vid})" if vid else ""))


def coin_add(vid, peso):
    s = slots[vid]
    if not s["reserved"]:
        coin_orphan(vid, peso)
        return
    s["last"] = mock_now()
    coin_credit(s, peso)


def coin_finish(vid):
    s = slots[vid]
    data = UNLIMITED_BYTES if s["unlimited"] else s["bytes"]
    existing = sessions.get(s["extend"]) if s["extend"] else None
    extended = bool(existing and not existing.get("isSubscriber") and existing["mode"] == "hotspot")
    if extended:
        existing["remainingSeconds"] += s["seconds"]
        if existing["remainingBytes"] < UNLIMITED_BYTES:
            existing["remainingBytes"] = UNLIMITED_BYTES if data == UNLIMITED_BYTES else existing["remainingBytes"] + data
        code = s["extend"]  # a paused session stays paused - Resume picks up the new totals
    else:
        code = "ZX" + "".join(random.choice(CODE_ALPHABET) for _ in range(6))
        sessions[code] = {
            "mode": "hotspot", "tier": s["tier"], "remainingSeconds": s["seconds"],
            "remainingBytes": data, "lastTick": time.time(), "paused": False, "isSubscriber": False,
            "pauseWindowMinutes": s["pauseWindow"],
        }
        admin_stats["usersToday"] += 1
    add_coin_revenue(vid, s["pesos"])
    log_event("coin_extend" if extended else "coin",
              f"{code} PHP {s['pesos']} = {s['seconds'] // 60} min ({s['mac']})")
    s.update(doneToken=s["token"], doneCode=code, doneExtended=extended, doneAt=mock_now(), doneRid=s["rid"])
    coin_release(s)
    return code, extended


def coin_unclaimed(vid, peso):
    # The cash is in the box, so it counts as a sale - and the log says who
    # to refund if a customer turns up. Never silently dropped.
    add_coin_revenue(vid, peso)
    log_event("coin_late_unclaimed",
              f"PHP {peso} at vendo {vid} was never claimed - counted in sales; refund the customer if they ask")


def coin_tick():
    t = mock_now()
    for vid, s in list(slots.items()):
        if s["orphan"] and t - s["orphanAt"] >= COIN_ORPHAN_WINDOW_SEC:
            peso, s["orphan"] = s["orphan"], 0
            coin_unclaimed(vid, peso)
        if not s["reserved"]:
            continue
        idle = t - s["last"]
        if not s["seconds"]:
            if idle > COIN_NO_COIN_TIMEOUT_SEC:
                coin_release(s)
        elif idle > COIN_IDLE_TIMEOUT_SEC:
            coin_finish(vid)


def coin_done_result(token):
    for s in slots.values():
        if token and s["doneToken"] == token and mock_now() - s["doneAt"] < COIN_RESULT_KEEP_SEC:
            return s["doneCode"], s["doneExtended"]
    return None


def slot_for_token(token):
    for vid, s in slots.items():
        if token and s["reserved"] and s["token"] == token:
            return vid, s
    return None, None


def log_event(event_type, detail):
    activity_log.append({"epoch": int(time.time()), "type": event_type, "detail": detail})
    if len(activity_log) > 300:
        activity_log.pop(0)


def tick_sessions():
    now = time.time()
    for sid, s in list(sessions.items()):
        elapsed = now - s["lastTick"]
        s["lastTick"] = now

        if s.get("paused"):
            window = s.get("pauseWindowMinutes", MAX_PAUSE_MINUTES)
            if now - s.get("pausedAt", now) > window * 60:
                del sessions[sid]
            continue

        if s["remainingSeconds"] < UNLIMITED_SECONDS:
            s["remainingSeconds"] = max(0, s["remainingSeconds"] - elapsed)
        if s["remainingBytes"] < UNLIMITED_BYTES:
            simulated_usage = int(elapsed * 20000)  # ~20KB/sec pretend traffic
            s["remainingBytes"] = max(0, s["remainingBytes"] - simulated_usage)
            admin_stats["dataUsedTodayBytes"] += simulated_usage

        if s.get("isSubscriber"):
            sub = subscribers.get(sid)
            if not sub or not sub["active"] or sub["expiryEpoch"] <= now:
                del sessions[sid]
        elif s["remainingSeconds"] <= 0 or s["remainingBytes"] <= 0:
            del sessions[sid]


# Mirrors gui_handler.h's isLockedOut()/recordLoginFailure() - a small
# per-IP failure counter shared by /api/login and admin auth, so the
# brute-force-protection behavior stays testable without real hardware.
LOGIN_MAX_FAILURES = 8
LOGIN_LOCKOUT_SEC = 60
login_attempts = {}  # ip -> {failCount, lastFailTime}


def is_locked_out(ip):
    a = login_attempts.get(ip)
    if not a or a["failCount"] < LOGIN_MAX_FAILURES:
        return False
    if time.time() - a["lastFailTime"] > LOGIN_LOCKOUT_SEC:
        a["failCount"] = 0
        return False
    return True


def record_login_failure(ip):
    a = login_attempts.setdefault(ip, {"failCount": 0, "lastFailTime": 0})
    a["failCount"] += 1
    a["lastFailTime"] = time.time()


def record_login_success(ip):
    if ip in login_attempts:
        login_attempts[ip]["failCount"] = 0


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print("  ", fmt % args)

    def _set_cors_headers(self):
        # Mirrors gui_handler.h's setCorsHeaders() - in production this
        # GUI is served by MikroTik (a different origin from the
        # NodeMCU that implements this API), so every real request is
        # cross-origin; see script.js's apiBase(). Not needed for this
        # mock's own same-origin dev testing, but kept consistent so a
        # deliberately cross-origin test setup (two separate local
        # servers, one static-only, one API-only) behaves the same way
        # the real MikroTik+NodeMCU split does.
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, X-Admin-Username, X-Admin-Password, X-Client-Time")

    def _json(self, code, payload):
        body = json.dumps(payload).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self._set_cors_headers()
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):
        self.send_response(204)
        self._set_cors_headers()
        self.send_header("Content-Length", "0")
        self.end_headers()

    def _read_json_body(self):
        length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(length) if length else b""
        try:
            return json.loads(raw) if raw else {}
        except json.JSONDecodeError:
            return None

    def _require_admin(self, require_super=False):
        ip = self.client_address[0]
        if is_locked_out(ip):
            self._json(429, {"error": "too_many_attempts"})
            return None
        username = self.headers.get("X-Admin-Username", "")
        password = self.headers.get("X-Admin-Password", "")
        a = admins.get(username)
        if not a or not a["active"] or a["password"] != password:
            record_login_failure(ip)
            self._json(401, {"error": "unauthorized"})
            return None
        record_login_success(ip)
        if require_super and a["role"] != "super":
            self._json(403, {"error": "super_admin_required"})
            return None
        return {"username": username, **a}

    def do_GET(self):
        parsed = urlparse(self.path)
        path, qs = parsed.path, parse_qs(parsed.query)
        tick_sessions()
        coin_tick()

        if path == "/api/health":
            self._json(200, {"mikrotik": True, "uptimeMs": int(time.time() * 1000), "fw": FIRMWARE_VERSION})
        elif path == "/api/branding":
            self._json(200, {"brandName": settings["brandName"], "brandColor": settings["brandColor"],
                              "soundEnabled": settings["soundEnabled"], "announcement": settings["announcement"],
                              "rates": [{"peso": r["pesoAmount"], "minutes": r["minutes"], "dataMb": r["dataMb"],
                                         "validityMinutes": r["validityMinutes"]} for r in rate_profiles]})
        elif path == "/api/status":
            sid = (qs.get("session") or [""])[0]
            s = sessions.get(sid)
            if not s:
                self._json(404, {"error": "session_not_found"})
                return
            self._json(200, {
                "sessionId": sid, "mode": s["mode"], "tier": s["tier"],
                "timeRemainingSec": int(s["remainingSeconds"]),
                "dataRemainingBytes": s["remainingBytes"], "idle": False,
                "paused": s.get("paused", False), "isSubscriber": s.get("isSubscriber", False),
            })
        elif path == "/api/coin/status":
            token = (qs.get("token") or [""])[0]
            vid, s = slot_for_token(token)
            if s:
                idle = mock_now() - s["last"]
                self._json(200, {
                    "state": "inserting", "vendo": vid, "pesos": s["pesos"], "minutes": s["seconds"] // 60,
                    "dataMb": 0 if s["unlimited"] else s["bytes"] // (1024 * 1024),
                    "unmatchedPesos": s["unmatched"], "secondsLeft": max(0, int(coin_limit(s) - idle)),
                })
            elif coin_done_result(token):
                code, extended = coin_done_result(token)
                self._json(200, {"state": "done", "code": code, "extended": extended})
            else:
                self._json(200, {"state": "none"})
        elif path == "/api/admin/subscribers":
            admin = self._require_admin(require_super=True)
            if not admin:
                return
            now = time.time()
            self._json(200, [
                {"username": u, "tier": s["tier"], "expiryEpoch": int(s["expiryEpoch"]),
                 "active": s["active"], "expired": s["expiryEpoch"] <= now}
                for u, s in subscribers.items()
            ])
        elif path == "/api/admin/accounts":
            admin = self._require_admin(require_super=True)
            if not admin:
                return
            self._json(200, [
                {"username": u, "role": a["role"], "active": a["active"]}
                for u, a in admins.items()
            ])
        elif path == "/api/admin/logs":
            admin = self._require_admin()
            if not admin:
                return
            self._json(200, activity_log)
        elif path == "/api/admin/sales":
            admin = self._require_admin()
            if not admin:
                return
            today = {
                "dateStamp": time.strftime("%Y-%m-%d"),
                "coinRevenue": admin_stats["coinRevenueToday"],
                "voucherRevenue": admin_stats["voucherRevenueToday"],
                "subscriptionRevenue": admin_stats["subscriptionRevenueToday"],
                "users": admin_stats["usersToday"],
                "dataUsedBytes": admin_stats["dataUsedTodayBytes"],
            }
            self._json(200, {"history": sales_history, "today": today})
        elif path == "/api/admin/overview":
            admin = self._require_admin()
            if not admin:
                return
            revenue_today = (admin_stats["coinRevenueToday"] + admin_stats["voucherRevenueToday"]
                              + admin_stats["subscriptionRevenueToday"])
            self._json(200, {
                "totalUsers": admin_stats["usersToday"],
                "activeSessions": len(sessions),
                "dataUsedTodayBytes": admin_stats["dataUsedTodayBytes"],
                "revenueToday": revenue_today,
                "coinRevenueToday": admin_stats["coinRevenueToday"],
                "voucherRevenueToday": admin_stats["voucherRevenueToday"],
                "subscriptionRevenueToday": admin_stats["subscriptionRevenueToday"],
                "unusedVouchers": sum(1 for v in vouchers.values() if not v["used"]),
                "mikrotik": True,
                "role": admin["role"],
            })
        elif path == "/api/admin/tier-info":
            # Open to staff too (unlike /api/admin/settings, super-only) -
            # mirrors gui_handler.h's handleAdminTierInfo(): read-only
            # rate profiles for the Voucher Type dropdown, not the full
            # editable Settings.
            admin = self._require_admin()
            if not admin:
                return
            self._json(200, rate_profiles)
        elif path == "/api/admin/rate-profiles":
            admin = self._require_admin(require_super=True)
            if not admin:
                return
            self._json(200, rate_profiles)
        elif path == "/api/admin/blocked":
            admin = self._require_admin()
            if not admin:
                return
            self._json(200, blocked_macs)
        elif path == "/api/admin/users":
            admin = self._require_admin()
            if not admin:
                return
            self._json(200, [
                {"sessionId": sid, "mode": s["mode"], "tier": s["tier"],
                 "timeRemainingSec": int(s["remainingSeconds"]),
                 "dataRemainingBytes": s["remainingBytes"],
                 "paused": s.get("paused", False), "isSubscriber": s.get("isSubscriber", False)}
                for sid, s in sessions.items()
            ])
        elif path == "/api/admin/settings":
            admin = self._require_admin(require_super=True)
            if not admin:
                return
            self._json(200, settings)
        else:
            self._serve_static(path)

    def do_POST(self):
        parsed = urlparse(self.path)
        path = parsed.path
        tick_sessions()
        coin_tick()

        if path == "/login":
            # Stand-in for MikroTik's own hotspot walled-garden login
            # handler (see script.js's submitMikrotikHotspotLogin()) -
            # a real router validates username/password against
            # /ip/hotspot/user and only then releases that specific
            # client's traffic. This mock has no walled garden to
            # release, so it just simulates the redirect shape (a 302
            # to `dst`) so the GUI's click-through flow stays testable
            # without real MikroTik hardware.
            length = int(self.headers.get("Content-Length", 0))
            raw = self.rfile.read(length) if length else b""
            fields = parse_qs(raw.decode())
            username = fields.get("username", [""])[0]
            dst = fields.get("dst", ["status.html"])[0]
            print(f"  [mock /login] {username} -> redirecting to {dst}")
            self.send_response(302)
            self.send_header("Location", dst)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        elif path == "/api/login":
            ip = self.client_address[0]
            if is_locked_out(ip):
                self._json(429, {"error": "too_many_attempts"})
                return
            body = self._read_json_body()
            if body is None:
                self._json(400, {"error": "bad_json"})
                return
            code = body.get("code", "").strip()
            password = body.get("password", "")
            mode = body.get("mode", "hotspot")

            sub = subscribers.get(code)
            now = time.time()
            if sub and sub["active"] and sub["expiryEpoch"] > now and sub["password"] == password:
                sessions[code] = {
                    "mode": mode, "tier": sub["tier"],
                    "remainingSeconds": UNLIMITED_SECONDS, "remainingBytes": UNLIMITED_BYTES,
                    "lastTick": now, "paused": False, "isSubscriber": True,
                }
                log_event("subscriber_login", f"{code} ({mode})")
                record_login_success(ip)
                self._json(200, {
                    "sessionId": code, "mode": mode, "tier": sub["tier"],
                    "timeRemainingSec": UNLIMITED_SECONDS, "dataRemainingBytes": UNLIMITED_BYTES,
                    "nightPromoApplied": False, "isSubscriber": True,
                })
                return

            # Reconnect: a device resubmitting a code that already has an
            # active session (e.g. a phone that randomized its MAC and
            # re-triggered the walled garden) resumes with its current
            # remaining budget instead of being rejected as
            # "invalid_or_used_voucher" just because it's already used -
            # mirrors gui_handler.h's handleLogin() reconnect branch.
            code = code.upper()
            existing = sessions.get(code)
            if existing and not existing.get("isSubscriber"):
                if existing.get("paused"):
                    existing["paused"] = False
                    existing["lastTick"] = now
                log_event("voucher_reconnect", f"{code} ({existing['mode']})")
                record_login_success(ip)
                self._json(200, {
                    "sessionId": code, "mode": existing["mode"], "tier": existing["tier"],
                    "timeRemainingSec": int(existing["remainingSeconds"]),
                    "dataRemainingBytes": existing["remainingBytes"],
                    "nightPromoApplied": False, "isSubscriber": False,
                })
                return

            v = vouchers.get(code)
            if not v or v["used"] or (v.get("expiryEpoch", 0) and v["expiryEpoch"] < time.time()):
                record_login_failure(ip)
                self._json(400, {"error": "invalid_or_used_voucher"})
                return
            v["used"] = True
            # The voucher's own baked-in price/time/data (set at
            # generation time from the Rate Profile that existed then),
            # matching AdminAPI::VoucherRecord - a printed/generated
            # voucher's value doesn't change if rate profiles change later.
            data_bytes = v["dataBytes"]
            time_seconds = v["timeSeconds"]
            night_applied = False  # Night Promo's live clock check isn't simulated here

            sessions[code] = {
                "mode": mode, "tier": v["tier"],
                "remainingSeconds": time_seconds, "remainingBytes": data_bytes,
                "lastTick": now, "paused": False, "isSubscriber": False,
                "pauseWindowMinutes": v.get("pauseWindowMinutes", MAX_PAUSE_MINUTES),
            }
            admin_stats["usersToday"] += 1
            admin_stats["voucherRevenueToday"] += v["price"]
            log_event("voucher_login", f"{code} tier {v['tier']} ({mode})")
            record_login_success(ip)
            self._json(200, {
                "sessionId": code, "mode": mode, "tier": v["tier"],
                "timeRemainingSec": time_seconds, "dataRemainingBytes": data_bytes,
                "nightPromoApplied": night_applied, "isSubscriber": False,
            })
        elif path == "/api/pause":
            body = self._read_json_body() or {}
            s = sessions.get(body.get("session", ""))
            if not s:
                self._json(404, {"error": "session_not_found"})
                return
            if s.get("paused"):
                self._json(400, {"error": "already_paused"})
                return
            s["paused"] = True
            s["pausedAt"] = time.time()
            self._json(200, {"ok": True})
        elif path == "/api/resume":
            body = self._read_json_body() or {}
            s = sessions.get(body.get("session", ""))
            if not s:
                self._json(404, {"error": "session_not_found"})
                return
            if not s.get("paused"):
                self._json(400, {"error": "not_paused"})
                return
            s["paused"] = False
            s["lastTick"] = time.time()
            self._json(200, {"ok": True})
        elif path == "/api/admin/subscribers":
            admin = self._require_admin(require_super=True)
            if not admin:
                return
            body = self._read_json_body() or {}
            username = body.get("username", "").strip()
            password = body.get("password", "")
            tier = str(body.get("tier", "1"))
            days = int(body.get("days", 30))
            price = float(body.get("price", 0))
            if not any(sp["id"] == tier for sp in settings["speedProfiles"]):
                self._json(400, {"error": "unknown_speed_profile"})
                return
            if not username or not password:
                self._json(400, {"error": "missing_username_or_password"})
                return
            if username in subscribers:
                self._json(400, {"error": "username_taken_or_table_full"})
                return
            subscribers[username] = {
                "password": password, "tier": tier,
                "expiryEpoch": time.time() + days * 86400, "active": True,
            }
            admin_stats["subscriptionRevenueToday"] += price
            log_event("subscriber_added", f"{username} by {admin['username']}")
            self._json(200, {"ok": True})
        elif path == "/api/admin/subscribers/renew":
            admin = self._require_admin(require_super=True)
            if not admin:
                return
            body = self._read_json_body() or {}
            sub = subscribers.get(body.get("username", ""))
            if not sub:
                self._json(404, {"error": "subscriber_not_found"})
                return
            days = int(body.get("days", 30))
            price = float(body.get("price", 0))
            base = max(sub["expiryEpoch"], time.time())
            sub["expiryEpoch"] = base + days * 86400
            sub["active"] = True
            admin_stats["subscriptionRevenueToday"] += price
            log_event("subscriber_renewed", f"{body.get('username','')} +{days}d by {admin['username']}")
            self._json(200, {"ok": True})
        elif path == "/api/admin/subscribers/toggle":
            admin = self._require_admin(require_super=True)
            if not admin:
                return
            body = self._read_json_body() or {}
            sub = subscribers.get(body.get("username", ""))
            if not sub:
                self._json(404, {"error": "subscriber_not_found"})
                return
            sub["active"] = bool(body.get("active", True))
            log_event("subscriber_toggled", f"{body.get('username','')} -> {'active' if sub['active'] else 'disabled'} by {admin['username']}")
            self._json(200, {"ok": True})
        elif path == "/api/admin/accounts":
            admin = self._require_admin(require_super=True)
            if not admin:
                return
            body = self._read_json_body() or {}
            username = body.get("username", "").strip()
            password = body.get("password", "")
            role = body.get("role", "staff")
            if not username or not password:
                self._json(400, {"error": "missing_username_or_password"})
                return
            if username in admins or role not in ("super", "staff"):
                self._json(400, {"error": "username_taken_invalid_role_or_table_full"})
                return
            admins[username] = {"password": password, "role": role, "active": True}
            log_event("admin_account_added", f"{username} ({role}) by {admin['username']}")
            self._json(200, {"ok": True})
        elif path == "/api/admin/accounts/toggle":
            admin = self._require_admin(require_super=True)
            if not admin:
                return
            body = self._read_json_body() or {}
            target_username = body.get("username", "")
            target = admins.get(target_username)
            active = bool(body.get("active", True))
            if not target:
                self._json(400, {"error": "not_found_or_last_super_admin"})
                return
            active_supers = sum(1 for a in admins.values() if a["active"] and a["role"] == "super")
            if not active and target["role"] == "super" and target["active"] and active_supers <= 1:
                self._json(400, {"error": "not_found_or_last_super_admin"})
                return
            target["active"] = active
            log_event("admin_account_toggled", f"{target_username} -> {'active' if active else 'disabled'} by {admin['username']}")
            self._json(200, {"ok": True})
        elif path == "/api/extend":
            body = self._read_json_body()
            sid, code = body.get("session", ""), body.get("code", "").strip().upper()
            s = sessions.get(sid)
            if not s:
                self._json(404, {"error": "session_not_found"})
                return
            if s.get("isSubscriber"):
                self._json(400, {"error": "subscriptions_dont_extend"})
                return
            v = vouchers.get(code)
            if not v or v["used"] or (v.get("expiryEpoch", 0) and v["expiryEpoch"] < time.time()):
                self._json(400, {"error": "invalid_or_used_voucher"})
                return
            v["used"] = True
            s["remainingSeconds"] += v["timeSeconds"]
            s["remainingBytes"] += v["dataBytes"]
            admin_stats["voucherRevenueToday"] += v["price"]
            log_event("extend", f"{sid} +{code}")
            self._json(200, {
                "sessionId": sid, "timeRemainingSec": int(s["remainingSeconds"]),
                "dataRemainingBytes": s["remainingBytes"],
            })
        elif path == "/api/coin/start":
            body = self._read_json_body() or {}
            mac = body.get("mac", "")
            if mac and any(m.lower() == mac.lower() for m in blocked_macs):
                self._json(403, {"error": "device_blocked"})
                return
            vid = body.get("vendo", 0)
            if not isinstance(vid, int) or isinstance(vid, bool) or vid not in slots:
                self._json(404, {"error": "vendo_unknown"})
                return
            s = slots[vid]
            token = body.get("token", "")
            if s["reserved"] and token and token == s["token"]:
                s["last"] = mock_now()
                self._json(200, {"token": token, "timeoutSec": COIN_NO_COIN_TIMEOUT_SEC, "vendo": vid})
                return
            if s["reserved"]:
                idle = mock_now() - s["last"]
                self._json(409, {"error": "coin_slot_busy", "waitSec": max(1, int(coin_limit(s) - idle) + 1)})
                return
            coin_clear_credit(s)
            RID["next"] += 1
            s.update(reserved=True, token="".join(random.choice(CODE_ALPHABET) for _ in range(12)),
                     mac=mac, extend=body.get("session", ""), last=mock_now(), rid=RID["next"])
            if s["orphan"]:  # coin_tick() already expired stale ones
                peso, s["orphan"] = s["orphan"], 0
                coin_credit(s, peso)
                log_event("coin_orphan_claimed", f"PHP {peso} by {mac}")
            self._json(200, {"token": s["token"], "timeoutSec": COIN_NO_COIN_TIMEOUT_SEC, "vendo": vid})
        elif path == "/api/coin/done":
            token = (self._read_json_body() or {}).get("token", "")
            vid, s = slot_for_token(token)
            if coin_done_result(token):
                code, extended = coin_done_result(token)
            elif not s:
                self._json(400, {"error": "no_coin_session"})
                return
            elif not s["seconds"]:
                coin_release(s)
                self._json(400, {"error": "no_coins_inserted"})
                return
            else:
                code, extended = coin_finish(vid)
            sess = sessions.get(code, {})
            self._json(200, {"sessionId": code, "mode": "hotspot", "extended": extended,
                             "timeRemainingSec": int(sess.get("remainingSeconds", 0)),
                             "dataRemainingBytes": sess.get("remainingBytes", 0)})
        elif path == "/api/coin/cancel":
            token = (self._read_json_body() or {}).get("token", "")
            vid, s = slot_for_token(token)
            if s and not s["seconds"]:
                coin_release(s)
            self._json(200, {"ok": True})
        elif path == "/dev/coin":
            # Dev-only: one coin drop of {"peso": N} on the main unit's own
            # acceptor (vendo 0). No firmware equivalent.
            body = self._read_json_body() or {}
            vid = body.get("vendo", 0)
            if vid not in slots:
                self._json(404, {"error": "vendo_unknown"})
                return
            coin_add(vid, int(body.get("peso", 0)))
            self._json(200, {"ok": True})
        elif path == "/dev/clock":
            # Dev-only test clock - see mock_now().
            CLOCK["offset"] += float((self._read_json_body() or {}).get("advance", 0))
            coin_tick()
            self._json(200, {"offset": CLOCK["offset"]})
        elif path == "/api/disconnect":
            body = self._read_json_body()
            sid = body.get("session", "")
            if sid in sessions:
                del sessions[sid]
                log_event("disconnect", sid)
            self._json(200, {"ok": True})
        elif path == "/api/admin/kick":
            # Deliberately separate from /api/disconnect (the customer's
            # own self-service path, unauthenticated by design) - this
            # one requires admin auth, mirroring gui_handler.h's
            # handleAdminKick() fix for the same gap.
            admin = self._require_admin()
            if not admin:
                return
            body = self._read_json_body() or {}
            sid = body.get("session", "")
            if sid in sessions:
                del sessions[sid]
                log_event("admin_kick", f"{sid} by {admin['username']}")
            self._json(200, {"ok": True})
        elif path == "/api/admin/block":
            admin = self._require_admin()
            if not admin:
                return
            body = self._read_json_body() or {}
            sid = body.get("session", "")
            # No real MikroTik here to look up the session's actual MAC -
            # the mock fabricates a stand-in so the flow is clickable.
            mac = f"MOCK-MAC-{sid}"
            if sid in sessions:
                del sessions[sid]
            if mac not in blocked_macs:
                blocked_macs.append(mac)
            log_event("admin_block", f"{sid} ({mac}) by {admin['username']}")
            self._json(200, {"ok": True, "mac": mac, "mikrotikBlocked": True})
        elif path == "/api/admin/unblock":
            admin = self._require_admin()
            if not admin:
                return
            body = self._read_json_body() or {}
            mac = body.get("mac", "")
            if mac in blocked_macs:
                blocked_macs.remove(mac)
            log_event("admin_unblock", f"{mac} by {admin['username']}")
            self._json(200, {"ok": True, "mikrotikUnblocked": True})
        elif path == "/api/admin/settings":
            admin = self._require_admin(require_super=True)
            if not admin:
                return
            body = self._read_json_body() or {}
            # Same validation as GUIHandler::handleAdminSaveSettings.
            speeds = body.get("speedProfiles", settings["speedProfiles"])
            if not speeds:
                self._json(400, {"error": "need_one_speed_profile"})
                return
            if len(speeds) > 8:
                self._json(400, {"error": "too_many_speed_profiles"})
                return
            ids = [str(sp.get("id", "")) for sp in speeds]
            if len(set(ids)) != len(ids):
                self._json(400, {"error": "duplicate_speed_profile"})
                return
            for sp in speeds:
                if not sp.get("name") or float(sp.get("downMbps", 0)) <= 0 or float(sp.get("upMbps", 0)) <= 0:
                    self._json(400, {"error": "invalid_speed_profile"})
                    return
            rates = body.get("rateProfiles", rate_profiles)
            seen = set()
            for r in rates:
                if not r.get("pesoAmount") or not r.get("minutes"):
                    self._json(400, {"error": "invalid_profile"})
                    return
                if r["pesoAmount"] in seen:
                    self._json(400, {"error": "duplicate_peso_amount"})
                    return
                seen.add(r["pesoAmount"])
                if str(r.get("speedProfile", "1")) not in ids:
                    self._json(400, {"error": "unknown_speed_profile" if "rateProfiles" in body else "speed_profile_in_use"})
                    return
            if any(sub["tier"] not in ids for sub in subscribers.values()):
                self._json(400, {"error": "speed_profile_in_use"})
                return
            pins = ("D1", "D2", "D5", "D6", "D7")
            coin_pin, relay_pin = body.get("coinPin", settings["coinPin"]), body.get("relayPin", settings["relayPin"])
            if coin_pin not in pins or (relay_pin != "none" and relay_pin not in pins):
                self._json(400, {"error": "invalid_pin"})
                return
            if coin_pin == relay_pin:
                self._json(400, {"error": "coin_and_relay_same_pin"})
                return
            if not 1 <= int(body.get("coinPulseValue", settings["coinPulseValue"])) <= 100:
                self._json(400, {"error": "invalid_pulse_value"})
                return
            settings.update({k: body[k] for k in settings if k in body})
            settings["speedProfiles"] = [dict(sp, id=str(sp["id"])) for sp in speeds]
            settings["announcement"] = settings["announcement"][:300]
            if "rateProfiles" in body:
                rate_profiles.clear()
                rate_profiles.extend(rates)
            log_event("settings_changed", f"by {admin['username']}")
            # Mock router is always "reachable", so this mirrors the
            # firmware's mikrotikPushed=true path (real profile pushes
            # aren't simulated here - see mikrotik_api.h for the actual
            # /ip/hotspot/user/profile/set + /ppp/profile/set calls).
            self._json(200, {"ok": True, "mikrotikPushed": True})
        elif path == "/api/admin/rate-profiles":
            admin = self._require_admin(require_super=True)
            if not admin:
                return
            body = self._read_json_body()
            if not isinstance(body, list):
                self._json(400, {"error": "expected_array"})
                return
            if len(body) > 20:  # MAX_RATE_PROFILES
                self._json(400, {"error": "too_many_profiles"})
                return
            seen = set()
            for p in body:
                if not p.get("pesoAmount") or not p.get("minutes"):
                    self._json(400, {"error": "invalid_profile"})
                    return
                if p["pesoAmount"] in seen:
                    self._json(400, {"error": "duplicate_peso_amount"})
                    return
                seen.add(p["pesoAmount"])
                if str(p.get("speedProfile", "1")) not in [sp["id"] for sp in settings["speedProfiles"]]:
                    self._json(400, {"error": "unknown_speed_profile"})
                    return
            rate_profiles.clear()
            rate_profiles.extend(body)
            log_event("rate_profiles_changed", f"by {admin['username']} ({len(body)} profiles)")
            self._json(200, {"ok": True})
        elif path == "/api/admin/vouchers/import":
            admin = self._require_admin()
            if not admin:
                return
            length = int(self.headers.get("Content-Length", 0))
            csv_text = self.rfile.read(length).decode() if length else ""
            imported = 0
            for i, line in enumerate(csv_text.splitlines()):
                if i == 0 or not line.strip():
                    continue
                cols = line.split(",")
                if len(cols) < 6 or cols[0] in vouchers:
                    continue
                vouchers[cols[0]] = {
                    "tier": cols[1], "used": False,
                    "price": float(cols[3]), "timeSeconds": int(cols[4]), "dataBytes": int(cols[5]),
                    "expiryEpoch": int(cols[8]) if len(cols) > 8 and cols[8] else 0,
                    "pauseWindowMinutes": int(cols[9]) if len(cols) > 9 and cols[9] else MAX_PAUSE_MINUTES,
                }
                imported += 1
            self._json(200, {"imported": imported})
        elif path == "/api/admin/vouchers/generate":
            admin = self._require_admin()
            if not admin:
                return
            body = self._read_json_body() or {}
            peso_amount = int(body.get("pesoAmount", 0))
            count = min(int(body.get("count", 1)), 20)
            # Bakes in the *current* Rate Profile at generation time
            # (mirrors AdminAPI::generateVouchers()) - a later profile
            # change doesn't retroactively alter already-issued vouchers.
            profile = profile_by_peso(peso_amount)
            if not profile:
                self._json(400, {"error": "no_such_rate_profile"})
                return
            time_seconds = profile["minutes"] * 60
            data_bytes = profile["dataMb"] * 1024 * 1024 if profile["dataMb"] > 0 else UNLIMITED_BYTES
            validity_minutes = profile["validityMinutes"]
            expiry_epoch = int(time.time() + validity_minutes * 60) if validity_minutes > 0 else 0
            pause_window = validity_minutes if validity_minutes > 0 else MAX_PAUSE_MINUTES

            codes = []
            for _ in range(count):
                code = "ZX-" + "".join(random.choice("ABCDEFGHJKMNPQRSTUVWXYZ23456789") for _ in range(8))
                vouchers[code] = {
                    "tier": profile["speedProfile"], "used": False, "expiryEpoch": expiry_epoch,
                    "price": peso_amount, "timeSeconds": time_seconds, "dataBytes": data_bytes,
                    "pauseWindowMinutes": pause_window,
                }
                codes.append(code)
            self._json(200, codes)
        else:
            self._json(404, {"error": "not_found"})

    def _serve_static(self, path):
        if path == "/":
            path = "/login.html"
        file_path = (GUI_DIR / path.lstrip("/")).resolve()
        if GUI_DIR not in file_path.parents or not file_path.is_file():
            self._json(404, {"error": "not_found"})
            return
        content_type = {
            ".html": "text/html", ".css": "text/css", ".js": "application/javascript",
        }.get(file_path.suffix, "application/octet-stream")
        data = file_path.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    print(f"Mock ZxheiFi API + GUI server: http://localhost:{port}/login.html")
    print("Admin logins: admin/admin (super), cashier1/cashier123 (staff)")
    print("Test vouchers:", ", ".join(vouchers.keys()))
    print("Test subscriber: room5 / tenant123")
    print("Simulate a coin: POST /dev/coin {\"peso\": 10}  (after tapping Insert Coin)")
    ThreadingHTTPServer(("localhost", port), Handler).serve_forever()
