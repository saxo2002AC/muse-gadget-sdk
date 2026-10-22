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

#include "muse_dns.h"

#include <stdio.h>
#include <string.h>

/* One dotted IPv4 address at s[0..len), no leading zeros, not 0.0.0.0. */
static bool ipv4(const char *s, size_t len, uint32_t *out)
{
    uint32_t v = 0;
    int parts = 0;
    size_t i = 0;
    while (parts < 4) {
        size_t start = i;
        uint32_t octet = 0;
        while (i < len && s[i] >= '0' && s[i] <= '9' && i - start < 3) {
            octet = octet * 10 + (uint32_t)(s[i] - '0');
            i++;
        }
        size_t digits = i - start;
        if (!digits || octet > 255 || (digits > 1 && s[start] == '0')) {
            return false;
        }
        v = v << 8 | octet;
        parts++;
        if (parts < 4) {
            if (i >= len || s[i] != '.') {
                return false;
            }
            i++;
        }
    }
    if (i != len || !v) {
        return false;
    }
    *out = v;
    return true;
}

bool muse_dns_parse(const char *text, uint32_t servers[MUSE_DNS_MAX], int *n)
{
    *n = 0;
    if (!*text) {
        return true;
    }
    const char *s = text;
    for (;;) {
        const char *comma = strchr(s, ',');
        size_t len = comma ? (size_t)(comma - s) : strlen(s);
        if (*n >= MUSE_DNS_MAX || !ipv4(s, len, &servers[*n])) {
            *n = 0;
            return false;
        }
        (*n)++;
        if (!comma) {
            return true;
        }
        s = comma + 1;
    }
}

void muse_dns_format(const uint32_t *servers, int n, char *out, size_t cap)
{
    size_t at = 0;
    out[0] = '\0';
    for (int i = 0; i < n && at < cap; i++) {
        uint32_t v = servers[i];
        int w = snprintf(out + at, cap - at, "%s%u.%u.%u.%u", i ? "," : "", (unsigned)(v >> 24),
                         (unsigned)(v >> 16 & 255), (unsigned)(v >> 8 & 255), (unsigned)(v & 255));
        at += w > 0 ? (size_t)w : 0;
    }
}

void muse_dns_init(muse_dns_state_t *st, const uint32_t *ours, int n)
{
    memset(st, 0, sizeof(*st));
    muse_dns_set(st, ours, n);
}

void muse_dns_set(muse_dns_state_t *st, const uint32_t *ours, int n)
{
    n = n < 0 ? 0 : n > MUSE_DNS_MAX ? MUSE_DNS_MAX : n;
    memset(st->ours, 0, sizeof(st->ours));
    if (n) {
        memcpy(st->ours, ours, sizeof(st->ours[0]) * n);
    }
    st->n = n;
}

bool muse_dns_step(muse_dns_state_t *st, const uint32_t cur[3], uint32_t want[3])
{
    if (!st->n) {
        if (!st->applied) {
            return false;   /* never overrode: DHCP's are as DHCP set them */
        }
        want[0] = st->dhcp;
        want[1] = want[2] = 0;
        if (!memcmp(cur, want, sizeof(uint32_t) * 3)) {
            st->applied = false;
            return false;
        }
        return true;
    }
    if (!cur[0] && !cur[1] && !cur[2]) {
        return false;   /* no lease yet */
    }
    if (cur[0] && cur[0] != st->ours[0]) {
        st->dhcp = cur[0];   /* DHCP put the router's first: a lease or a renewal */
    }
    want[0] = st->ours[0];
    want[1] = st->n > 1 ? st->ours[1] : st->dhcp;
    want[2] = st->n > 1 ? st->dhcp : 0;
    st->applied = true;
    return memcmp(cur, want, sizeof(uint32_t) * 3) != 0;
}
