/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "jolly/jolly_fast_auth.h"

#include <stdio.h>
#include <string.h>

/* ---- SHA-256 (FIPS 180-4) ---- */

typedef struct {
    uint32_t h[8];
    uint64_t bits;
    uint8_t buf[64];
    size_t n;
} sha_t;

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t ror(uint32_t x, int n)
{
    return x >> n | x << (32 - n);
}

static void sha_block(sha_t *s, const uint8_t *p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ w[i - 15] >> 3;
        uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ w[i - 2] >> 10;
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    s->h[0] += a;
    s->h[1] += b;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
    s->h[5] += f;
    s->h[6] += g;
    s->h[7] += h;
}

static void sha_init(sha_t *s)
{
    static const uint32_t H0[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    memcpy(s->h, H0, sizeof(H0));
    s->bits = 0;
    s->n = 0;
}

static void sha_update(sha_t *s, const void *data, size_t len)
{
    const uint8_t *p = data;
    s->bits += (uint64_t)len * 8;
    while (len) {
        if (!s->n && len >= 64) {
            sha_block(s, p);
            p += 64;
            len -= 64;
            continue;
        }
        size_t k = 64 - s->n < len ? 64 - s->n : len;
        memcpy(s->buf + s->n, p, k);
        s->n += k;
        p += k;
        len -= k;
        if (s->n == 64) {
            sha_block(s, s->buf);
            s->n = 0;
        }
    }
}

static void sha_final(sha_t *s, uint8_t out[32])
{
    uint64_t bits = s->bits;
    uint8_t pad = 0x80;
    sha_update(s, &pad, 1);
    pad = 0;
    while (s->n != 56) {
        sha_update(s, &pad, 1);
    }
    uint8_t len[8];
    for (int i = 0; i < 8; i++) {
        len[i] = (uint8_t)(bits >> (56 - 8 * i));
    }
    sha_update(s, len, 8);
    for (int i = 0; i < 8; i++) {
        out[4 * i] = (uint8_t)(s->h[i] >> 24);
        out[4 * i + 1] = (uint8_t)(s->h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(s->h[i] >> 8);
        out[4 * i + 3] = (uint8_t)s->h[i];
    }
}

void jolly_sha256(const void *data, size_t len, uint8_t out[32])
{
    sha_t s;
    sha_init(&s);
    sha_update(&s, data, len);
    sha_final(&s, out);
}

/* ---- HMAC (RFC 2104) ---- */

void jolly_hmac_sha256(const uint8_t *key, size_t key_len, const void *m1, size_t l1, const void *m2, size_t l2,
                       uint8_t out[32])
{
    uint8_t k[64] = { 0 }, pad[64], inner[32];
    if (key_len > 64) {
        jolly_sha256(key, key_len, k);
    } else {
        memcpy(k, key, key_len);
    }
    sha_t s;
    for (int i = 0; i < 64; i++) {
        pad[i] = k[i] ^ 0x36;
    }
    sha_init(&s);
    sha_update(&s, pad, 64);
    sha_update(&s, m1, l1);
    if (m2) {
        sha_update(&s, m2, l2);
    }
    sha_final(&s, inner);
    for (int i = 0; i < 64; i++) {
        pad[i] = k[i] ^ 0x5c;
    }
    sha_init(&s);
    sha_update(&s, pad, 64);
    sha_update(&s, inner, 32);
    sha_final(&s, out);
    jolly_wipe(k, sizeof(k));   /* the key's copy, and what's derived from it */
    jolly_wipe(pad, sizeof(pad));
    jolly_wipe(inner, sizeof(inner));
    jolly_wipe(&s, sizeof(s));
}

void jolly_wipe(void *p, size_t n)
{
    volatile uint8_t *v = p;
    while (n--) {
        *v++ = 0;
    }
}

/* ---- Hex ---- */

void jolly_hex(const uint8_t *in, size_t n, char *out)
{
    static const char D[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = D[in[i] >> 4];
        out[2 * i + 1] = D[in[i] & 15];
    }
    out[2 * n] = '\0';
}

static int nibble(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

bool jolly_unhex(const char *hex, uint8_t *out, size_t n)
{
    if (strlen(hex) != 2 * n) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        int hi = nibble(hex[2 * i]), lo = nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return true;
}

/* ---- The contract's signatures ---- */

void jolly_fast_canonical(long long ts, const char *nonce, const uint8_t body_sha[32], char *out, size_t cap)
{
    char h[65];
    jolly_hex(body_sha, 32, h);
    snprintf(out, cap, "POST|/v1/ask|%lld|%s|%s", ts, nonce, h);
}

void jolly_fast_auth(const uint8_t key[32], long long ts, const char *nonce, const uint8_t body_sha[32],
                     char out[65])
{
    char line[160];
    jolly_fast_canonical(ts, nonce, body_sha, line, sizeof(line));
    uint8_t d[32];
    jolly_hmac_sha256(key, 32, line, strlen(line), NULL, 0, d);
    jolly_hex(d, 32, out);
}

bool jolly_fast_sig_ok(const uint8_t key[32], const char *nonce, const char *body, size_t len, const char *sig_hex)
{
    uint8_t got[32], want[32];
    if (!sig_hex || !jolly_unhex(sig_hex, got, 32)) {
        return false;
    }
    char head[64];
    int n = snprintf(head, sizeof(head), "%s|", nonce);
    if (n <= 0 || (size_t)n >= sizeof(head)) {
        return false;
    }
    jolly_hmac_sha256(key, 32, head, (size_t)n, body, len, want);
    uint8_t diff = 0;   /* constant time */
    for (int i = 0; i < 32; i++) {
        diff |= got[i] ^ want[i];
    }
    return diff == 0;
}

bool jolly_fast_key_ok(const char *hex)
{
    uint8_t k[32];
    bool ok = jolly_unhex(hex, k, 32);
    jolly_wipe(k, sizeof(k));
    return ok;
}

bool jolly_fast_ms_ok(long ms)
{
    return ms >= 800 && ms <= 3000;
}

bool jolly_fast_max_s_ok(long s)
{
    return s >= 2 && s <= 15;
}
