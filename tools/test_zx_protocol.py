#!/usr/bin/env python3
"""
test_zx_protocol.py - Pins tools/zx_protocol.py (the Python reference of
the main <-> sub vendo protocol) to fixed vectors. The same vectors are
checked on the real chip by common/selftest/selftest.ino, so the Python
mock/simulator and the C++ firmware provably speak the same protocol.

Usage: python tools/test_zx_protocol.py
"""
import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import zx_protocol as zx  # noqa: E402

K0_HEX = "7410214d8edef1bf3a6262cdadb1c3b7900637133ef96a7064103d33472f38a8"
K_HEX = "1c4c3b7ce2a175b5fd466c49ec2e5ebac349c4a8a9d1856f1d22af0d2aea77a5"
SIG_HEX = "5567192daee74eaac073c34abe60349aebfc05e2b4905d31555b3aca1845490d"


class Vectors(unittest.TestCase):
    def test_rfc4231_case2(self):
        self.assertEqual(zx.hmac256(b"Jefe", b"what do ya want for nothing?").hex(),
                         "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843")

    def test_rfc4231_case6_long_key(self):
        self.assertEqual(zx.hmac256(b"\xaa" * 131, b"Test Using Larger Than Block-Size Key - Hash Key First").hex(),
                         "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54")

    def test_pair_key_normalizes(self):
        self.assertEqual(zx.pair_key("k7qm-2xpa-9rtd").hex(), K0_HEX)
        self.assertEqual(zx.pair_key("K7QM2XPA9RTD").hex(), K0_HEX)

    def test_vendo_key(self):
        k0 = bytes.fromhex(K0_HEX)
        self.assertEqual(zx.vendo_key(k0, 1, "AA:BB:CC:DD:EE:01", "0123456789abcdef").hex(), K_HEX)

    def test_sign(self):
        self.assertEqual(zx.sign(bytes.fromhex(K_HEX), b'{"v":1,"n":5}'), SIG_HEX)


class Verify(unittest.TestCase):
    key = bytes.fromhex(K_HEX)

    def test_accepts_good(self):
        self.assertTrue(zx.verify(self.key, b'{"v":1,"n":5}', SIG_HEX))
        self.assertTrue(zx.verify(self.key, b'{"v":1,"n":5}', SIG_HEX.upper()))

    def test_rejects_tampered_body(self):
        self.assertFalse(zx.verify(self.key, b'{"v":1,"n":6}', SIG_HEX))

    def test_rejects_bad_signature_shapes(self):
        self.assertFalse(zx.verify(self.key, b'{"v":1,"n":5}', SIG_HEX[:-2]))
        self.assertFalse(zx.verify(self.key, b'{"v":1,"n":5}', "zz" * 32))
        self.assertFalse(zx.verify(self.key, b'{"v":1,"n":5}', ""))
        self.assertFalse(zx.verify(self.key, b'{"v":1,"n":5}', None))


class Codes(unittest.TestCase):
    def test_normalize(self):
        self.assertEqual(zx.normalize_code(" k7qm-2xpa 9rtd "), "K7QM2XPA9RTD")

    def test_new_pair_code_format(self):
        pattern = re.compile(r"^[2-9A-HJ-NP-Z]{4}-[2-9A-HJ-NP-Z]{4}-[2-9A-HJ-NP-Z]{4}$")
        for _ in range(50):
            self.assertRegex(zx.new_pair_code(), pattern)


if __name__ == "__main__":
    unittest.main(verbosity=1)
