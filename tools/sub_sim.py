#!/usr/bin/env python3
"""
sub_sim.py - a scriptable sub vendo that speaks the real signed protocol
(common/zx_protocol.h <-> tools/zx_protocol.py), for tests and for poking a
real main unit without building a second box.

As a library (tools/vendo_test.py):
    sub = SubSim(port=8098); sub.pair("K7QM-2XPA-9RTD"); sub.poll()
    sub.drop_coin(10); sub.flush()

Against real hardware (PC joined to the ZxheiFi hotspot WiFi):
    python tools/sub_sim.py --host 10.0.0.254 --port 80 --code XXXX-XXXX-XXXX
    python tools/sub_sim.py --host 10.0.0.254 --port 80 --code XXXX-XXXX-XXXX --coin 5 --coin 10
It pairs, then polls like a real sub; each --coin is dropped once the main
reports the relay ON (i.e. after a customer taps Insert Coin on that box).
"""
import argparse
import http.client
import json
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import zx_protocol as zx  # noqa: E402


class SubSim:
    def __init__(self, host="127.0.0.1", port=8098, mac="AA:BB:CC:DD:EE:01"):
        self.host, self.port, self.mac = host, port, mac
        self.vid = 0
        self.key = None
        self.name = ""
        self.config = {}
        self.n = 0            # message counter (the real sub persists it in blocks)
        self.seq = 0          # coin sequence
        self.rid = 0          # reservation id from the last poll while the relay was on
        self.relay = False
        self.cfg_ver = 0
        self.queue = []       # [{"seq", "peso", "rid"}] not yet acknowledged
        self.last = None      # (status, data, verified) of the last call

    # -- transport -------------------------------------------------------
    def raw_post(self, path, payload, key, tamper=False):
        """Signed POST. Returns (status, data, reply_signature_ok)."""
        body = json.dumps(payload, separators=(",", ":")).encode()
        sig = zx.sign(key, body)
        if tamper:
            sig = ("0" if sig[0] != "0" else "1") + sig[1:]
        conn = http.client.HTTPConnection(self.host, self.port, timeout=5)
        conn.request("POST", path, body=body, headers={"Content-Type": "application/json", zx.SIG_HEADER: sig})
        resp = conn.getresponse()
        raw = resp.read()
        reply_sig = resp.getheader(zx.SIG_HEADER, "")
        conn.close()
        try:
            data = json.loads(raw) if raw else {}
        except json.JSONDecodeError:
            data = {}
        self.last = (resp.status, data, zx.verify(key, raw, reply_sig) if reply_sig else False)
        return self.last

    def next_n(self):
        self.n += 1
        return self.n

    # -- protocol --------------------------------------------------------
    def pair(self, code, nonce=None):
        nonce = nonce or os.urandom(8).hex()
        k0 = zx.pair_key(code)
        status, data, ok = self.raw_post("/api/vendo/pair", {"mac": self.mac, "nonce": nonce}, k0)
        if status == 200 and ok and data.get("nonce") == nonce:
            self.vid = data["vendoId"]
            self.key = zx.vendo_key(k0, self.vid, self.mac, nonce)
            self.name = data.get("name", "")
            self.config = data.get("config", {})
            self.cfg_ver = self.config.get("cfgVer", 0)
            self.seq = max(self.seq, data.get("lastCoinSeq", 0))
            self.n = 0
        return status, data, ok

    def poll(self):
        n = self.next_n()
        status, data, ok = self.raw_post("/api/vendo/poll", {"v": self.vid, "n": n}, self.key)
        if status == 200 and ok and data.get("n") == n:
            self.relay = bool(data.get("relay"))
            self.rid = data.get("rid", 0) if self.relay else 0
            if data.get("cfgVer", self.cfg_ver) != self.cfg_ver:
                self.fetch_config()
        return status, data, ok

    def fetch_config(self):
        n = self.next_n()
        status, data, ok = self.raw_post("/api/vendo/config", {"v": self.vid, "n": n}, self.key)
        if status == 200 and ok and data.get("n") == n:
            self.config = {k: v for k, v in data.items() if k != "n"}
            self.cfg_ver = data.get("cfgVer", self.cfg_ver)
        return status, data, ok

    def drop_coin(self, peso, rid=None):
        """A coin goes in: queued first (like the real sub), tagged with the
        reservation seen at that moment."""
        self.seq += 1
        self.queue.append({"seq": self.seq, "peso": peso, "rid": self.rid if rid is None else rid})

    def send_coin(self, coin):
        n = self.next_n()
        return self.raw_post("/api/vendo/coin", {"v": self.vid, "n": n, **coin}, self.key)

    def flush(self):
        """Sends queued coins in order; stops at the first unacknowledged one."""
        results = []
        while self.queue:
            status, data, ok = self.send_coin(self.queue[0])
            results.append((status, data, ok))
            if status == 200 and ok and data.get("ok"):
                self.queue.pop(0)
            else:
                break
        return results


class ChargeSim(SubSim):
    """A Charging Station box: reports port times in its polls, carries
    out admin Stops (acknowledging them) and sends charging sales."""

    def __init__(self, host="127.0.0.1", port=8098, mac="AA:BB:CC:DD:EE:C1"):
        super().__init__(host, port, mac)
        self.ports = [0, 0, 0, 0]   # remaining seconds per port
        self.last_stop_id = 0
        self.stopped = []           # ports stopped by the admin

    def poll(self):
        n = self.next_n()
        status, data, ok = self.raw_post("/api/vendo/poll",
                                         {"v": self.vid, "n": n, "ports": self.ports, "ackStop": self.last_stop_id},
                                         self.key)
        if status == 200 and ok and data.get("n") == n:
            for stop in data.get("stop", []):
                if stop["id"] > self.last_stop_id:
                    if 1 <= stop["port"] <= len(self.ports):
                        self.ports[stop["port"] - 1] = 0
                    self.stopped.append(stop["port"])
                    self.last_stop_id = stop["id"]
            if data.get("cfgVer", self.cfg_ver) != self.cfg_ver:
                self.fetch_config()
        return status, data, ok

    def charge(self, peso, port, minutes, seq=None):
        """A charging sale (queued then sent, like the real box)."""
        if seq is None:
            self.seq += 1
            seq = self.seq
        if 1 <= port <= len(self.ports):
            self.ports[port - 1] += minutes * 60
        n = self.next_n()
        return self.raw_post("/api/vendo/charge",
                             {"v": self.vid, "n": n, "seq": seq, "peso": peso, "port": port, "minutes": minutes},
                             self.key)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--mac", default="AA:BB:CC:DD:EE:99")
    ap.add_argument("--code", required=True, help="pairing code from Admin > Vendos > Add Vendo")
    ap.add_argument("--coin", type=int, action="append", default=[], help="peso value to drop (repeatable)")
    ap.add_argument("--seconds", type=float, default=120, help="how long to keep polling")
    args = ap.parse_args()

    sub = SubSim(args.host, args.port, args.mac)
    status, data, ok = sub.pair(args.code)
    print(f"pair -> HTTP {status}, reply signature {'OK' if ok else 'BAD'}: {data}")
    if not sub.key:
        sys.exit(1)
    pending = list(args.coin)
    deadline = time.time() + args.seconds
    while time.time() < deadline:
        status, data, ok = sub.poll()
        print(f"poll -> {status} relay={sub.relay} rid={sub.rid} sig={'OK' if ok else 'BAD'}")
        if sub.relay and pending:
            peso = pending.pop(0)
            sub.drop_coin(peso)
            print(f"  coin PHP {peso}: {sub.flush()}")
        time.sleep(zx.POLL_ACTIVE_MS / 1000 if sub.relay else zx.POLL_IDLE_MS / 1000)


if __name__ == "__main__":
    main()
