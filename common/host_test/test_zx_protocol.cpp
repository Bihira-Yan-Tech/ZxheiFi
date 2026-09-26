// Host test for common/zx_protocol.h - same vectors as
// tools/test_zx_protocol.py and common/selftest/selftest.ino.
// Build + run (PlatformIO's MinGW toolchain):
//   python tools/run_host_tests.py
#define ARDUINO 1
#include <cstdio>
#include "Arduino.h"
#include "../zx_protocol.h"

static int fails = 0;

static void check(const char* name, const String& got, const char* want) {
  bool ok = got == want;
  if (!ok) fails++;
  printf("%s %s\n", ok ? "PASS" : "FAIL", name);
  if (!ok) printf("  got  %s\n  want %s\n", got.c_str(), want);
}

static void checkTrue(const char* name, bool cond) {
  if (!cond) fails++;
  printf("%s %s\n", cond ? "PASS" : "FAIL", name);
}

int main() {
  uint8_t out[32];

  zx::sha256((const uint8_t*)"abc", 3, out);
  check("sha256(abc)", zx::hexOf(out, 32), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

  // 56..64-byte messages exercise the extra padding block.
  const char* m56 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  zx::sha256((const uint8_t*)m56, strlen(m56), out);
  check("sha256(448-bit)", zx::hexOf(out, 32), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

  zx::hmacSha256((const uint8_t*)"Jefe", 4, (const uint8_t*)"what do ya want for nothing?", 28, out);
  check("rfc4231-2", zx::hexOf(out, 32), "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");

  uint8_t key6[131];
  memset(key6, 0xaa, sizeof(key6));
  const char* m6 = "Test Using Larger Than Block-Size Key - Hash Key First";
  zx::hmacSha256(key6, sizeof(key6), (const uint8_t*)m6, strlen(m6), out);
  check("rfc4231-6", zx::hexOf(out, 32), "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");

  check("normalizeCode", zx::normalizeCode(" k7qm-2xpa 9rtd "), "K7QM2XPA9RTD");

  uint8_t k0[32];
  zx::pairKey("k7qm-2xpa-9rtd", k0);
  check("pairKey", zx::hexOf(k0, 32), "7410214d8edef1bf3a6262cdadb1c3b7900637133ef96a7064103d33472f38a8");

  uint8_t k[32];
  zx::vendoKey(k0, 1, "AA:BB:CC:DD:EE:01", "0123456789abcdef", k);
  check("vendoKey", zx::hexOf(k, 32), "1c4c3b7ce2a175b5fd466c49ec2e5ebac349c4a8a9d1856f1d22af0d2aea77a5");

  const char* sig = "5567192daee74eaac073c34abe60349aebfc05e2b4905d31555b3aca1845490d";
  check("sign", zx::sign(k, "{\"v\":1,\"n\":5}"), sig);
  checkTrue("verify good", zx::verify(k, "{\"v\":1,\"n\":5}", sig));
  checkTrue("verify uppercase sig", zx::verify(k, "{\"v\":1,\"n\":5}",
            "5567192DAEE74EAAC073C34ABE60349AEBFC05E2B4905D31555B3ACA1845490D"));
  checkTrue("verify tampered", !zx::verify(k, "{\"v\":1,\"n\":6}", sig));
  checkTrue("verify short sig", !zx::verify(k, "{\"v\":1,\"n\":5}", String(sig).substring(2)));
  checkTrue("verify non-hex", !zx::verify(k, "{\"v\":1,\"n\":5}",
            "zz67192daee74eaac073c34abe60349aebfc05e2b4905d31555b3aca1845490d"));
  checkTrue("verify empty", !zx::verify(k, "{\"v\":1,\"n\":5}", ""));

  uint8_t back[32];
  checkTrue("fromHex roundtrip", zx::fromHex(zx::hexOf(k, 32).c_str(), back, 32) && zx::equalConstTime(back, k, 32));

  printf("HOST TEST %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
  return fails ? 1 : 0;
}
