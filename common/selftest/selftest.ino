/*
 * selftest.ino - checks common/zx_protocol.h on the real chip against the
 * same vectors as tools/test_zx_protocol.py (this PC has no host C++
 * compiler, so the device is where the C++ gets tested).
 *
 *   pio run -d common/selftest -t upload --upload-port COM5
 *   python tools/serial_watch.py COM5 --expect "SELFTEST PASSED"
 *
 * The result repeats every 2 s so a serial reader attached late sees it.
 * Reflash the real firmware afterwards (this only replaces the app; the
 * device's saved data is untouched).
 */
#include <Arduino.h>
#include "zx_protocol.h"

static int fails = 0;
static String report;

static void check(const char* name, const String& got, const char* want) {
  bool ok = got == want;
  if (!ok) fails++;
  report += String(ok ? "PASS " : "FAIL ") + name + "\n";
  if (!ok) report += "  got  " + got + "\n  want " + want + "\n";
}

void setup() {
  Serial.begin(115200);
  uint8_t out[32];

  zx::hmacSha256((const uint8_t*)"Jefe", 4, (const uint8_t*)"what do ya want for nothing?", 28, out);
  check("rfc4231-2", zx::hexOf(out, 32), "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");

  uint8_t key6[131];
  memset(key6, 0xaa, sizeof(key6));
  const char* m6 = "Test Using Larger Than Block-Size Key - Hash Key First";
  zx::hmacSha256(key6, sizeof(key6), (const uint8_t*)m6, strlen(m6), out);
  check("rfc4231-6", zx::hexOf(out, 32), "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");

  uint8_t k0[32];
  zx::pairKey("k7qm-2xpa-9rtd", k0);
  check("pairKey", zx::hexOf(k0, 32), "7410214d8edef1bf3a6262cdadb1c3b7900637133ef96a7064103d33472f38a8");

  uint8_t k[32];
  zx::vendoKey(k0, 1, "AA:BB:CC:DD:EE:01", "0123456789abcdef", k);
  check("vendoKey", zx::hexOf(k, 32), "1c4c3b7ce2a175b5fd466c49ec2e5ebac349c4a8a9d1856f1d22af0d2aea77a5");

  const char* sig = "5567192daee74eaac073c34abe60349aebfc05e2b4905d31555b3aca1845490d";
  check("sign", zx::sign(k, "{\"v\":1,\"n\":5}"), sig);

  bool good = zx::verify(k, "{\"v\":1,\"n\":5}", sig);
  bool tampered = zx::verify(k, "{\"v\":1,\"n\":6}", sig);
  bool shortSig = zx::verify(k, "{\"v\":1,\"n\":5}", String(sig).substring(2));
  check("verify", String(good && !tampered && !shortSig ? "ok" : "bad"), "ok");
}

void loop() {
  Serial.print(report);
  Serial.printf("SELFTEST %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
  delay(2000);
}
