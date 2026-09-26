/*
 * zx_protocol.h - main <-> sub vendo protocol, shared by both firmwares
 * ZXHEIFI
 *
 * Every request and reply is JSON signed with
 *   X-ZX-Sig = hex(HMAC-SHA256(key, exact raw body))
 * Pairing: the admin's one-time code (Admin > Vendos > Add Vendo) gives
 *   K0 = HMAC(normalized code, "zxheifi-pair-v1")
 * and the per-vendo key is
 *   K  = HMAC(K0, "key|<vendoId>|<sub MAC>|<sub nonce>")
 * so no key ever crosses the (open) WiFi. Replays are refused by a
 * per-vendo message counter "n"; coins carry a sequence number "seq" so a
 * resend is never credited twice.
 *
 * SHA-256/HMAC are implemented here (no core crypto dependency), so the
 * ESP8266 and ESP32 builds behave identically. Pinned to the same vectors
 * as tools/zx_protocol.py - see common/selftest/selftest.ino.
 */
#ifndef ZX_PROTOCOL_H
#define ZX_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#ifdef ARDUINO
#include <Arduino.h>
#endif

#define ZX_PAIR_CONTEXT        "zxheifi-pair-v1"
#define ZX_SIG_HEADER          "X-ZX-Sig"
#define ZX_POLL_IDLE_MS        2000
#define ZX_POLL_ACTIVE_MS      1000
#define ZX_SUB_FAILCLOSED_MS   3000
#define ZX_VENDO_OFFLINE_MS    10000
#define ZX_PAIR_CODE_TTL_MS    900000UL
#define ZX_COIN_RESEND_MS      2000
#define ZX_COIN_QUEUE_MAX      20
#define ZX_MAX_PENDING_PAIRS   2
#define ZX_PAIR_FAIL_LIMIT     5
#define ZX_PAIR_FAIL_WINDOW_MS 60000UL
#define ZX_OFFLINE_ALERT_MS    300000UL

namespace zx {

class Sha256 {
public:
  void init() {
    static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(_h, iv, sizeof(_h));
    _bits = 0;
    _used = 0;
  }

  void update(const uint8_t* p, size_t n) {
    _bits += (uint64_t)n * 8;
    while (n) {
      size_t take = 64 - _used;
      if (take > n) take = n;
      memcpy(_buf + _used, p, take);
      _used += take;
      p += take;
      n -= take;
      if (_used == 64) {
        block(_buf);
        _used = 0;
      }
    }
  }

  void final(uint8_t out[32]) {
    uint64_t bits = _bits;
    _buf[_used++] = 0x80;
    if (_used > 56) {
      while (_used < 64) _buf[_used++] = 0;
      block(_buf);
      _used = 0;
    }
    while (_used < 56) _buf[_used++] = 0;
    for (int i = 7; i >= 0; i--) _buf[_used++] = (uint8_t)(bits >> (8 * i));
    block(_buf);
    for (int i = 0; i < 8; i++) {
      out[4 * i] = (uint8_t)(_h[i] >> 24);
      out[4 * i + 1] = (uint8_t)(_h[i] >> 16);
      out[4 * i + 2] = (uint8_t)(_h[i] >> 8);
      out[4 * i + 3] = (uint8_t)_h[i];
    }
  }

private:
  uint32_t _h[8];
  uint8_t _buf[64];
  uint64_t _bits = 0;
  size_t _used = 0;

  static uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

  void block(const uint8_t* p) {
    static const uint32_t k[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
      w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    }
    for (int i = 16; i < 64; i++) {
      uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = _h[0], b = _h[1], c = _h[2], d = _h[3], e = _h[4], f = _h[5], g = _h[6], h = _h[7];
    for (int i = 0; i < 64; i++) {
      uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
      uint32_t ch = (e & f) ^ (~e & g);
      uint32_t t1 = h + S1 + ch + k[i] + w[i];
      uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
      uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t t2 = S0 + mj;
      h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    _h[0] += a; _h[1] += b; _h[2] += c; _h[3] += d;
    _h[4] += e; _h[5] += f; _h[6] += g; _h[7] += h;
  }
};

inline void sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
  Sha256 s;
  s.init();
  s.update(data, len);
  s.final(out);
}

inline void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t msgLen, uint8_t out[32]) {
  uint8_t k[64];
  memset(k, 0, sizeof(k));
  if (keyLen > 64) sha256(key, keyLen, k);
  else memcpy(k, key, keyLen);
  uint8_t pad[64], inner[32];
  Sha256 s;
  for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
  s.init();
  s.update(pad, 64);
  s.update(msg, msgLen);
  s.final(inner);
  for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
  s.init();
  s.update(pad, 64);
  s.update(inner, 32);
  s.final(out);
}

// out must hold 2*n+1 chars.
inline void toHex(const uint8_t* b, size_t n, char* out) {
  static const char* digits = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out[2 * i] = digits[b[i] >> 4];
    out[2 * i + 1] = digits[b[i] & 0x0f];
  }
  out[2 * n] = 0;
}

inline int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Exactly 2*n hex chars expected.
inline bool fromHex(const char* hex, uint8_t* out, size_t n) {
  if (!hex || strlen(hex) != 2 * n) return false;
  for (size_t i = 0; i < n; i++) {
    int hi = hexNibble(hex[2 * i]), lo = hexNibble(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)(hi << 4 | lo);
  }
  return true;
}

inline bool equalConstTime(const uint8_t* a, const uint8_t* b, size_t n) {
  uint8_t diff = 0;
  for (size_t i = 0; i < n; i++) diff |= a[i] ^ b[i];
  return diff == 0;
}

#ifdef ARDUINO
inline String hexOf(const uint8_t* b, size_t n) {
  char buf[65];
  if (n > 32) n = 32;
  toHex(b, n, buf);
  return String(buf);
}

// Uppercase, letters and digits only - dashes/spaces are cosmetic.
inline String normalizeCode(const String& code) {
  String out;
  for (size_t i = 0; i < code.length(); i++) {
    char c = code[i];
    if (c >= 'a' && c <= 'z') c -= 32;
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) out += c;
  }
  return out;
}

inline void pairKey(const String& code, uint8_t k0[32]) {
  String n = normalizeCode(code);
  hmacSha256((const uint8_t*)n.c_str(), n.length(), (const uint8_t*)ZX_PAIR_CONTEXT, strlen(ZX_PAIR_CONTEXT), k0);
}

inline void vendoKey(const uint8_t k0[32], uint8_t vendoId, const String& mac, const String& nonce, uint8_t out[32]) {
  String msg = "key|" + String(vendoId) + "|" + mac + "|" + nonce;
  hmacSha256(k0, 32, (const uint8_t*)msg.c_str(), msg.length(), out);
}

inline String sign(const uint8_t key[32], const String& body) {
  uint8_t mac[32];
  hmacSha256(key, 32, (const uint8_t*)body.c_str(), body.length(), mac);
  return hexOf(mac, 32);
}

inline bool verify(const uint8_t key[32], const String& body, const String& sigHex) {
  uint8_t given[32], mac[32];
  if (!fromHex(sigHex.c_str(), given, 32)) return false;
  hmacSha256(key, 32, (const uint8_t*)body.c_str(), body.length(), mac);
  return equalConstTime(mac, given, 32);
}
#endif // ARDUINO

} // namespace zx

#endif // ZX_PROTOCOL_H
