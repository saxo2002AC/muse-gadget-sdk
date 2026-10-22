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
 * DNS servers of our own choosing ahead of the router's: a router whose DNS
 * drops queries (the ES3C40P's home one) makes getaddrinfo() fail with
 * EAI_FAIL and a chat turn fail with it. The servers in use become ours
 * first, then the router's (DHCP's) as the last resort. DHCP sets its own
 * again on every renewal without an event, so the board checks and puts ours
 * back (muse_resolve.c). .local names go by mDNS and aren't affected.
 * Addresses are IPv4 in host order. Pure: tests/jolly/test_dns.c.
 */

#define MUSE_DNS_MAX 2             /* of ours */
#define MUSE_DNS_TEXT 32           /* "255.255.255.255,255.255.255.255" */

/* "a.b.c.d[,a.b.c.d]" strictly, or "" for none (DHCP's only). */
bool muse_dns_parse(const char *text, uint32_t servers[MUSE_DNS_MAX], int *n);
void muse_dns_format(const uint32_t *servers, int n, char *out, size_t cap);

typedef struct {
    uint32_t ours[MUSE_DNS_MAX];
    int n;
    uint32_t dhcp;                 /* the router's, as DHCP last set it */
    bool applied;                  /* ours are (or were) in place */
} muse_dns_state_t;

void muse_dns_init(muse_dns_state_t *st, const uint32_t *ours, int n);

/* A new list of ours (n 0: give the router's back). */
void muse_dns_set(muse_dns_state_t *st, const uint32_t *ours, int n);

/* From the servers in use (main, backup, fallback; 0 for none), the ones to
 * set: true when they differ and `want` should be applied. */
bool muse_dns_step(muse_dns_state_t *st, const uint32_t cur[3], uint32_t want[3]);

#ifdef __cplusplus
}
#endif
