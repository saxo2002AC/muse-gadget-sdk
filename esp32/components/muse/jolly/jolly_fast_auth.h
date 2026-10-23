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

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The atajo's authentication, contract v1.1 (tests/jolly/contract/README.md).
 * The board never sends its key: anyone answering the Mini's .local name
 * (mDNS has no authentication, and there's no DHCP reservation) learns
 * nothing reusable.
 *
 *   request   X-Jolly-Ts     unix seconds (SNTP: no clock, no atajo)
 *             X-Jolly-Nonce  32 hex, random per request
 *             X-Jolly-Auth   hex HMAC-SHA256(key, "POST|/v1/ask|<ts>|<nonce>|<hex sha256(body)>")
 *   response  X-Jolly-Sig    hex HMAC-SHA256(key, "RESP|<nonce>|<status>|<hex sha256(body)>"):
 *                            checked before the answer is parsed (Diana C15)
 *
 * Contract v1.1 (Fernando's, tests/jolly/contract/atajo/). The key is 64 hex
 * digits; HMAC takes its 32 bytes. SHA-256 and HMAC are here, portable, so
 * the host tests run them (tests/jolly/test_fast_auth.c: FIPS 180-2, RFC
 * 4231 and the contract's auth.vectors.json). Pure.
 */

#define JOLLY_FAST_KEY_HEX 64

void jolly_sha256(const void *data, size_t len, uint8_t out[32]);

/* HMAC-SHA256 over m1 then m2 (m2 may be NULL). */
void jolly_hmac_sha256(const uint8_t *key, size_t key_len, const void *m1, size_t l1, const void *m2, size_t l2,
                       uint8_t out[32]);

/* Zeroes n bytes in a way the optimiser can't drop (keys, derived pads). */
void jolly_wipe(void *p, size_t n);

/* Lowercase hex of n bytes into out (2n + 1). */
void jolly_hex(const uint8_t *in, size_t n, char *out);

/* Exactly 2n hex digits into n bytes. */
bool jolly_unhex(const char *hex, uint8_t *out, size_t n);

/* The request's signed line, "POST|/v1/ask|<ts>|<nonce>|<hex body sha>". */
void jolly_fast_canonical(long long ts, const char *nonce, const uint8_t body_sha[32], char *out, size_t cap);

/* X-Jolly-Auth for it (64 hex + NUL). */
void jolly_fast_auth(const uint8_t key[32], long long ts, const char *nonce, const uint8_t body_sha[32],
                     char out[65]);

/* The response's X-Jolly-Sig is right for this request's nonce, this status and this body. */
bool jolly_fast_sig_ok(const uint8_t key[32], const char *nonce, int status, const char *body, size_t len,
                       const char *sig_hex);

/* Config: 64 hex digits; the deadline 800..3000 ms; the longest note asked about, 2..15 s. */
bool jolly_fast_key_ok(const char *hex);
bool jolly_fast_ms_ok(long ms);
bool jolly_fast_max_s_ok(long s);

#ifdef __cplusplus
}
#endif
