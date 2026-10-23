/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * jolly_fast_auth: the atajo's request and response authentication (contract
 * v1.1, tests/jolly/contract/README.md): the board never sends its key, it
 * signs each request (X-Jolly-Auth) and checks the answer's signature
 * (X-Jolly-Sig) before speaking it. Plus the config's validators.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "jolly/jolly_fast_auth.h"

static void hex_is(const uint8_t *d, size_t n, const char *want)
{
    char h[2 * 64 + 1];
    jolly_hex(d, n, h);
    if (strcmp(h, want)) {
        fprintf(stderr, "got  %s\nwant %s\n", h, want);
        assert(0);
    }
}

static void sha256(void)
{
    uint8_t d[32];
    jolly_sha256("", 0, d);
    hex_is(d, 32, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    jolly_sha256("abc", 3, d);
    hex_is(d, 32, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char *m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";   /* two blocks */
    jolly_sha256(m, strlen(m), d);
    hex_is(d, 32, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    static uint8_t mil[1000000];                                         /* a million 'a's */
    memset(mil, 'a', sizeof(mil));
    jolly_sha256(mil, sizeof(mil), d);
    hex_is(d, 32, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

static void hmac(void)
{
    /* RFC 4231 test cases 1, 2 and 6 (a key longer than a block). */
    uint8_t k1[20], d[32];
    memset(k1, 0x0b, sizeof(k1));
    jolly_hmac_sha256(k1, sizeof(k1), "Hi There", 8, NULL, 0, d);
    hex_is(d, 32, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    jolly_hmac_sha256((const uint8_t *)"Jefe", 4, "what do ya want ", 16, "for nothing?", 12, d);   /* in two parts */
    hex_is(d, 32, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    uint8_t k6[131];
    memset(k6, 0xaa, sizeof(k6));
    const char *m6 = "Test Using Larger Than Block-Size Key - Hash Key First";
    jolly_hmac_sha256(k6, sizeof(k6), m6, strlen(m6), NULL, 0, d);
    hex_is(d, 32, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
}

static const char *KEY_HEX = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

static void request_auth(void)
{
    uint8_t key[32];
    assert(jolly_unhex(KEY_HEX, key, 32));
    uint8_t body_sha[32];
    jolly_sha256("\x01\x00\x02\x00", 4, body_sha);
    char auth[65], line[256];
    jolly_fast_canonical(1761200000LL, "00112233445566778899aabbccddeeff", body_sha, line, sizeof(line));
    char bh[65];
    jolly_hex(body_sha, 32, bh);
    char want[256];
    snprintf(want, sizeof(want), "POST|/v1/ask|1761200000|00112233445566778899aabbccddeeff|%s", bh);
    assert(!strcmp(line, want));
    jolly_fast_auth(key, 1761200000LL, "00112233445566778899aabbccddeeff", body_sha, auth);
    uint8_t d[32];
    jolly_hmac_sha256(key, 32, want, strlen(want), NULL, 0, d);
    hex_is(d, 32, auth);
    assert(strlen(auth) == 64);
    /* A different timestamp, nonce or body: a different signature. */
    char other[65];
    jolly_fast_auth(key, 1761200001LL, "00112233445566778899aabbccddeeff", body_sha, other);
    assert(strcmp(auth, other));
}

static void response_sig(void)
{
    uint8_t key[32];
    assert(jolly_unhex(KEY_HEX, key, 32));
    const char *nonce = "00112233445566778899aabbccddeeff";
    const char *body = "{\"v\":1,\"action\":\"local\"}";
    /* HMAC(K, "RESP|<nonce>|<status>|<hex sha256(body)>") */
    uint8_t sha[32], d[32];
    char sha_hex[65], line[160], sig[65];
    jolly_sha256(body, strlen(body), sha);
    jolly_hex(sha, 32, sha_hex);
    snprintf(line, sizeof(line), "RESP|%s|200|%s", nonce, sha_hex);
    jolly_hmac_sha256(key, 32, line, strlen(line), NULL, 0, d);
    jolly_hex(d, 32, sig);
    assert(jolly_fast_sig_ok(key, nonce, 200, body, strlen(body), sig));
    assert(!jolly_fast_sig_ok(key, nonce, 500, body, strlen(body), sig));   /* bound to the status */
    sig[0] = sig[0] == '0' ? '1' : '0';                                     /* one digit off */
    assert(!jolly_fast_sig_ok(key, nonce, 200, body, strlen(body), sig));
    jolly_hex(d, 32, sig);
    assert(!jolly_fast_sig_ok(key, "ffeeddccbbaa99887766554433221100", 200, body, strlen(body), sig));
    assert(!jolly_fast_sig_ok(key, nonce, 200, body, strlen(body) - 1, sig));   /* a body cut short */
    assert(!jolly_fast_sig_ok(key, nonce, 200, body, strlen(body), ""));        /* no signature */
    assert(!jolly_fast_sig_ok(key, nonce, 200, body, strlen(body), "zz"));
    for (char *c = sig; *c; c++) {                                             /* uppercase hex is fine */
        *c = (char)(*c >= 'a' && *c <= 'f' ? *c - 32 : *c);
    }
    assert(jolly_fast_sig_ok(key, nonce, 200, body, strlen(body), sig));
}

/* contract/atajo/auth.vectors.json: Fernando's public test vector, checked
 * against openssl. The constants below are its values. */
static void contract_vectors(const char *dir)
{
    uint8_t key[32];
    assert(jolly_unhex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key, 32));
    /* Request: body = bytes(range(256)) * 75. */
    static uint8_t body[19200];
    for (size_t i = 0; i < sizeof(body); i++) {
        body[i] = (uint8_t)(i % 256);
    }
    uint8_t sha[32];
    jolly_sha256(body, sizeof(body), sha);
    hex_is(sha, 32, "cf514f4b5a3dbf1b064dd612afefa92cc2d579e9a029b7a0e41d24585ddb59d4");
    char line[200], auth[65];
    jolly_fast_canonical(1792771200LL, "00112233445566778899aabbccddeeff", sha, line, sizeof(line));
    assert(!strcmp(line, "POST|/v1/ask|1792771200|00112233445566778899aabbccddeeff|"
                         "cf514f4b5a3dbf1b064dd612afefa92cc2d579e9a029b7a0e41d24585ddb59d4"));
    jolly_fast_auth(key, 1792771200LL, "00112233445566778899aabbccddeeff", sha, auth);
    assert(!strcmp(auth, "c95d2c30a9dc9656353e05708a9f02bed368231eb77f9c3a497a844652a5a942"));
    /* Response: 200, the body of ask.local.hora.json. */
    char path[512], resp[1100];
    snprintf(path, sizeof(path), "%s/contract/atajo/ask.local.hora.json", dir);
    FILE *f = fopen(path, "rb");
    assert(f);
    size_t n = fread(resp, 1, sizeof(resp), f);
    fclose(f);
    jolly_sha256(resp, n, sha);
    hex_is(sha, 32, "db77e601feec782dbb118f241db63260b5fe5ac3105af81af1d2885fbf0f4e2d");
    assert(jolly_fast_sig_ok(key, "00112233445566778899aabbccddeeff", 200, resp, n,
                             "7d329392d8aa61af83b962ec7e43cd0f879301426b3f7a2f7fb2bae8bf83c8ce"));
}

static void config(void)
{
    assert(jolly_fast_key_ok(KEY_HEX));
    assert(jolly_fast_key_ok("ABCDEF0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789"));
    assert(!jolly_fast_key_ok(""));
    assert(!jolly_fast_key_ok("0001"));                                  /* 64 hex digits */
    assert(!jolly_fast_key_ok("g00102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1"));
    assert(!jolly_fast_key_ok("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f0"));
    assert(jolly_fast_ms_ok(800) && jolly_fast_ms_ok(1500) && jolly_fast_ms_ok(3000));
    assert(!jolly_fast_ms_ok(799) && !jolly_fast_ms_ok(3001));
    assert(jolly_fast_max_s_ok(2) && jolly_fast_max_s_ok(15) && !jolly_fast_max_s_ok(1) && !jolly_fast_max_s_ok(16));
    char n[33];
    uint8_t r[16] = { 0, 1, 2, 0xfe, 0xff };
    jolly_hex(r, 16, n);
    assert(strlen(n) == 32 && !strncmp(n, "000102feff", 10));
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    contract_vectors(argv[1]);
    sha256();
    hmac();
    request_auth();
    response_sig();
    config();
    return 0;
}
