// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from BookSync; Ed25519 field/group arithmetic derives from public-domain TweetNaCl.
#include "Ed25519Verify.h"

#include <Logging.h>
#include <Memory.h>

#include <cstring>

#if defined(ESP_PLATFORM) && !defined(SIMULATOR)
#include "mbedtls/sha512.h"
#define BOOKSYNC_ED25519_MBEDTLS_SHA512 1
#else
#define BOOKSYNC_ED25519_MBEDTLS_SHA512 0
#endif

namespace booksync::ed25519 {

namespace {

// --- SHA-512 ------------------------------------------------------------------------------------
#if !BOOKSYNC_ED25519_MBEDTLS_SHA512
constexpr uint64_t K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL,
    0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
    0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL, 0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
    0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL, 0x983e5152ee66dfabULL,
    0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL,
    0x53380d139d95b3dfULL, 0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
    0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL, 0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
    0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL,
    0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL, 0xca273eceea26619cULL,
    0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
    0x113f9804bef90daeULL, 0x1b710b35131c471bULL, 0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
    0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};

uint64_t rotr(uint64_t x, int c) { return (x >> c) | (x << (64 - c)); }

uint64_t load64(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
  return v;
}

void block(uint64_t h[8], const uint8_t* p) {
  uint64_t w[16], a[8];
  for (int i = 0; i < 16; ++i) w[i] = load64(p + 8 * i);
  for (int i = 0; i < 8; ++i) a[i] = h[i];
  for (int i = 0; i < 80; ++i) {
    if (i >= 16) {
      const uint64_t w15 = w[(i + 1) & 15], w2 = w[(i + 14) & 15];
      w[i & 15] +=
          (rotr(w15, 1) ^ rotr(w15, 8) ^ (w15 >> 7)) + w[(i + 9) & 15] + (rotr(w2, 19) ^ rotr(w2, 61) ^ (w2 >> 6));
    }
    const uint64_t t1 =
        a[7] + (rotr(a[4], 14) ^ rotr(a[4], 18) ^ rotr(a[4], 41)) + ((a[4] & a[5]) ^ (~a[4] & a[6])) + K[i] + w[i & 15];
    const uint64_t t2 =
        (rotr(a[0], 28) ^ rotr(a[0], 34) ^ rotr(a[0], 39)) + ((a[0] & a[1]) ^ (a[0] & a[2]) ^ (a[1] & a[2]));
    for (int j = 7; j > 0; --j) a[j] = a[j - 1];
    a[4] += t1;
    a[0] = t1 + t2;
  }
  for (int i = 0; i < 8; ++i) h[i] += a[i];
}
#endif

// --- Field arithmetic mod 2^255-19: 16 limbs of 16 bits (TweetNaCl) ------------------------------
using gf = int64_t[16];

// Constants are stored as 16-bit limbs (a quarter of the flash) and widened where used.

constexpr uint16_t D2[16] = {0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0,
                             0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406};
constexpr uint16_t D[16] = {0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070,
                            0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203};
constexpr uint16_t X[16] = {0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c,
                            0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169};
constexpr uint16_t Y[16] = {0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
                            0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666};
constexpr uint16_t SQRTM1[16] = {0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43,
                                 0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83};
// The group order L, little-endian.
constexpr uint8_t L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
                           0xa2, 0xde, 0xf9, 0xde, 0x14, 0,    0,    0,    0,    0,    0,
                           0,    0,    0,    0,    0,    0,    0,    0,    0,    0x10};

void set(gf r, const int64_t a[16]) { memcpy(r, a, sizeof(gf)); }
void widen(gf r, const uint16_t a[16]) {
  for (int i = 0; i < 16; ++i) r[i] = a[i];
}
void setSmall(gf r, int64_t v) {
  memset(r, 0, sizeof(gf));
  r[0] = v;
}

void carry(gf o) {
  for (int i = 0; i < 16; ++i) {
    o[i] += (int64_t)1 << 16;
    const int64_t c = o[i] >> 16;
    o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
    o[i] -= c * ((int64_t)1 << 16);
  }
}

void select(gf p, gf q, int b) {
  const int64_t c = ~(int64_t)(b - 1);
  for (int i = 0; i < 16; ++i) {
    const int64_t t = c & (p[i] ^ q[i]);
    p[i] ^= t;
    q[i] ^= t;
  }
}

void pack25519(uint8_t* o, const gf n) {
  gf m, t;
  set(t, n);
  carry(t);
  carry(t);
  carry(t);
  for (int j = 0; j < 2; ++j) {
    m[0] = t[0] - 0xffed;
    for (int i = 1; i < 15; ++i) {
      m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
      m[i - 1] &= 0xffff;
    }
    m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
    const int b = (int)((m[15] >> 16) & 1);
    m[14] &= 0xffff;
    select(t, m, 1 - b);
  }
  for (int i = 0; i < 16; ++i) {
    o[2 * i] = (uint8_t)(t[i] & 0xff);
    o[2 * i + 1] = (uint8_t)(t[i] >> 8);
  }
}

bool equal(const gf a, const gf b) {
  uint8_t c[32], d[32];
  pack25519(c, a);
  pack25519(d, b);
  return memcmp(c, d, 32) == 0;
}

int parity(const gf a) {
  uint8_t d[32];
  pack25519(d, a);
  return d[0] & 1;
}

void unpack25519(gf o, const uint8_t* n) {
  for (int i = 0; i < 16; ++i) o[i] = n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
  o[15] &= 0x7fff;
}

void add(gf o, const gf a, const gf b) {
  for (int i = 0; i < 16; ++i) o[i] = a[i] + b[i];
}
void sub(gf o, const gf a, const gf b) {
  for (int i = 0; i < 16; ++i) o[i] = a[i] - b[i];
}

void mul(gf o, const gf a, const gf b) {
  int64_t t[31] = {0};
  for (int i = 0; i < 16; ++i)
    for (int j = 0; j < 16; ++j) t[i + j] += a[i] * b[j];
  for (int i = 0; i < 15; ++i) t[i] += 38 * t[i + 16];
  for (int i = 0; i < 16; ++i) o[i] = t[i];
  carry(o);
  carry(o);
}
void squareField(gf o, const gf a) { mul(o, a, a); }

// a^(2^252-3), for the square root in unpack.
void pow2523(gf o, const gf i) {
  gf c;
  set(c, i);
  for (int a = 250; a >= 0; --a) {
    squareField(c, c);
    if (a != 1) mul(c, c, i);
  }
  set(o, c);
}

void invert(gf o, const gf i) {
  gf c;
  set(c, i);
  for (int a = 253; a >= 0; --a) {
    squareField(c, c);
    if (a != 2 && a != 4) mul(c, c, i);
  }
  set(o, c);
}

// --- Points in extended coordinates (X, Y, Z, T) -------------------------------------------------
using point = gf[4];

void pointAdd(point p, point q) {
  gf a, b, c, d, t, e, f, g, h;
  sub(a, p[1], p[0]);
  sub(t, q[1], q[0]);
  mul(a, a, t);
  add(b, p[0], p[1]);
  add(t, q[0], q[1]);
  mul(b, b, t);
  mul(c, p[3], q[3]);
  widen(t, D2);
  mul(c, c, t);
  mul(d, p[2], q[2]);
  add(d, d, d);
  sub(e, b, a);
  sub(f, d, c);
  add(g, d, c);
  add(h, b, a);
  mul(p[0], e, f);
  mul(p[1], h, g);
  mul(p[2], g, f);
  mul(p[3], e, h);
}

void pointSwap(point p, point q, int b) {
  for (int i = 0; i < 4; ++i) select(p[i], q[i], b);
}

void pointPack(uint8_t* r, point p) {
  gf tx, ty, zi;
  invert(zi, p[2]);
  mul(tx, p[0], zi);
  mul(ty, p[1], zi);
  pack25519(r, ty);
  r[31] ^= (uint8_t)(parity(tx) << 7);
}

// p = s * q (q is used as scratch).
void scalarMult(point p, point q, const uint8_t* s) {
  setSmall(p[0], 0);
  setSmall(p[1], 1);
  setSmall(p[2], 1);
  setSmall(p[3], 0);
  for (int i = 255; i >= 0; --i) {
    const int b = (s[i / 8] >> (i & 7)) & 1;
    pointSwap(p, q, b);
    pointAdd(q, p);
    pointAdd(p, p);
    pointSwap(p, q, b);
  }
}

void scalarBase(point p, point q, const uint8_t* s) {
  widen(q[0], X);
  widen(q[1], Y);
  setSmall(q[2], 1);
  mul(q[3], q[0], q[1]);
  scalarMult(p, q, s);
}

// r = -A for the encoded point; false when it is not on the curve.
bool unpackNeg(point r, const uint8_t p[32]) {
  gf t, chk, num, den, den2, den4, den6;
  setSmall(r[2], 1);
  unpack25519(r[1], p);
  squareField(num, r[1]);
  widen(t, D);
  mul(den, num, t);
  sub(num, num, r[2]);
  add(den, r[2], den);
  squareField(den2, den);
  squareField(den4, den2);
  mul(den6, den4, den2);
  mul(t, den6, num);
  mul(t, t, den);
  pow2523(t, t);
  mul(t, t, num);
  mul(t, t, den);
  mul(t, t, den);
  mul(r[0], t, den);
  squareField(chk, r[0]);
  mul(chk, chk, den);
  if (!equal(chk, num)) {
    widen(t, SQRTM1);
    mul(r[0], r[0], t);
  }
  squareField(chk, r[0]);
  mul(chk, chk, den);
  if (!equal(chk, num)) return false;
  if (parity(r[0]) == (p[31] >> 7)) {
    gf zero = {0};
    sub(r[0], zero, r[0]);
  }
  mul(r[3], r[0], r[1]);
  return true;
}

// r (64 bytes in, 32 out) = r mod L.
void reduce(uint8_t* r) {
  int64_t x[64];
  for (int i = 0; i < 64; ++i) x[i] = r[i];
  for (int i = 63; i >= 32; --i) {
    int64_t c = 0;
    int j;
    for (j = i - 32; j < i - 12; ++j) {
      x[j] += c - 16 * x[i] * L[j - (i - 32)];
      c = (x[j] + 128) >> 8;
      x[j] -= c * 256;
    }
    x[j] += c;
    x[i] = 0;
  }
  int64_t c = 0;
  for (int j = 0; j < 32; ++j) {
    x[j] += c - (x[31] >> 4) * L[j];
    c = x[j] >> 8;
    x[j] &= 255;
  }
  for (int j = 0; j < 32; ++j) x[j] -= c * L[j];
  for (int i = 0; i < 32; ++i) {
    x[i + 1] += x[i] >> 8;
    r[i] = (uint8_t)(x[i] & 255);
  }
}

// S < L (RFC 8032 §5.1.7: a larger S is rejected, so a signature has one encoding).
bool canonical(const uint8_t s[32]) {
  for (int i = 31; i >= 0; --i) {
    if (s[i] < L[i]) return true;
    if (s[i] > L[i]) return false;
  }
  return false;
}

struct Work {
  point p, q, base;
  uint8_t buffer[64 + kMaxMessage];
  uint8_t h[64];
};

}  // namespace

void sha512(const uint8_t* data, size_t length, uint8_t out[64]) {
#if BOOKSYNC_ED25519_MBEDTLS_SHA512
  mbedtls_sha512(data, length, out, 0);
#else
  uint64_t h[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
                   0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
  size_t done = 0;
  for (; length - done >= 128; done += 128) block(h, data + done);
  uint8_t last[256] = {0};
  const size_t rest = length - done;
  if (rest) memcpy(last, data + done, rest);
  last[rest] = 0x80;
  const size_t blocks = rest < 112 ? 1 : 2;
  const uint64_t bits = (uint64_t)length << 3;
  for (int i = 0; i < 8; ++i) last[blocks * 128 - 1 - i] = (uint8_t)(bits >> (8 * i));
  for (size_t b = 0; b < blocks; ++b) block(h, last + 128 * b);
  for (int i = 0; i < 8; ++i)
    for (int j = 0; j < 8; ++j) out[8 * i + j] = (uint8_t)(h[i] >> (56 - 8 * j));
#endif
}

bool verify(const uint8_t signature[64], const uint8_t* message, size_t length, const uint8_t publicKey[32]) {
  if (!signature || !publicKey || (!message && length) || length > kMaxMessage || !canonical(signature + 32))
    return false;
  // Cold verification only: bounded 1920-byte work area avoids overflowing C3 task stacks.
  auto w = makeUniqueNoThrow<Work>();
  if (!w) {
    LOG_ERR("UPDATE", "Cannot allocate signature work area");
    return false;
  }
  bool ok = unpackNeg(w->q, publicKey);
  if (ok) {
    // k = SHA-512(R || A || M) mod L; then [S]B - [k]A must encode to R.
    memcpy(w->buffer, signature, 32);
    memcpy(w->buffer + 32, publicKey, 32);
    if (length) memcpy(w->buffer + 64, message, length);
    sha512(w->buffer, 64 + length, w->h);
    reduce(w->h);
    scalarMult(w->p, w->q, w->h);
    scalarBase(w->q, w->base, signature + 32);
    pointAdd(w->p, w->q);
    uint8_t r[32];
    pointPack(r, w->p);
    ok = memcmp(r, signature, 32) == 0;
  }

  return ok;
}

}  // namespace booksync::ed25519
