"""
zx_protocol.py - Python reference of the main <-> sub vendo protocol.

Mirrors common/zx_protocol.h (the C++ used by both firmwares). Used by
tools/mock_server.py (the main unit's test double), tools/sub_sim.py (a
scriptable sub vendo) and their tests. tools/test_zx_protocol.py pins it to
fixed vectors that common/selftest checks on the real chip too.

Every request and reply carries X-ZX-Sig = hex HMAC-SHA256(key, raw body).
Pairing: the admin's one-time code gives K0 = HMAC(code, "zxheifi-pair-v1");
the vendo key is K = HMAC(K0, "key|<id>|<mac>|<nonce>"). Neither key ever
travels over the (open) WiFi.
"""
import hashlib
import hmac
import secrets

SIG_HEADER = "X-ZX-Sig"
PAIR_CONTEXT = b"zxheifi-pair-v1"
CODE_ALPHABET = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ"

# Timings/limits - keep identical to common/zx_protocol.h.
POLL_IDLE_MS = 2000
POLL_ACTIVE_MS = 1000
SUB_FAILCLOSED_MS = 3000
VENDO_OFFLINE_MS = 10000
PAIR_CODE_TTL_MS = 900000
COIN_RESEND_MS = 2000
COIN_QUEUE_MAX = 20
MAX_PENDING_PAIRS = 2
PAIR_FAIL_LIMIT = 5
PAIR_FAIL_WINDOW_MS = 60000
OFFLINE_ALERT_MS = 300000


def hmac256(key: bytes, msg: bytes) -> bytes:
    return hmac.new(key, msg, hashlib.sha256).digest()


def normalize_code(code: str) -> str:
    """Uppercase, letters and digits only - dashes/spaces are cosmetic."""
    return "".join(ch for ch in str(code).upper() if ch.isascii() and ch.isalnum())


def pair_key(code: str) -> bytes:
    return hmac256(normalize_code(code).encode(), PAIR_CONTEXT)


def vendo_key(k0: bytes, vid: int, mac: str, nonce: str) -> bytes:
    return hmac256(k0, f"key|{vid}|{mac}|{nonce}".encode())


def sign(key: bytes, body: bytes) -> str:
    return hmac256(key, body).hex()


def verify(key: bytes, body: bytes, sig_hex) -> bool:
    if not isinstance(sig_hex, str) or len(sig_hex) != 64:
        return False
    try:
        given = bytes.fromhex(sig_hex)
    except ValueError:
        return False
    return hmac.compare_digest(hmac256(key, body), given)


def new_pair_code() -> str:
    raw = "".join(secrets.choice(CODE_ALPHABET) for _ in range(12))
    return f"{raw[0:4]}-{raw[4:8]}-{raw[8:12]}"
