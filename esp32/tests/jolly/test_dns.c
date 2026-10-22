/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * muse_dns: our own DNS servers over the router's (Andrés's router answers
 * unreliably), kept through DHCP renewals, the router's kept as fallback.
 */
#include <assert.h>
#include <string.h>

#include "muse_dns.h"

#define IP(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

static void parse(void)
{
    uint32_t s[MUSE_DNS_MAX];
    int n = -1;
    assert(muse_dns_parse("1.1.1.1,8.8.8.8", s, &n) && n == 2 && s[0] == IP(1, 1, 1, 1) && s[1] == IP(8, 8, 8, 8));
    assert(muse_dns_parse("9.9.9.9", s, &n) && n == 1 && s[0] == IP(9, 9, 9, 9));
    assert(muse_dns_parse("", s, &n) && n == 0);                     /* the router's, from DHCP */
    assert(!muse_dns_parse("1.1.1.1,8.8.8.8,9.9.9.9", s, &n));       /* two at most */
    assert(!muse_dns_parse("1.1.1", s, &n));
    assert(!muse_dns_parse("1.1.1.256", s, &n));
    assert(!muse_dns_parse("01.1.1.1", s, &n));                      /* no leading zeros */
    assert(!muse_dns_parse("1.1.1.1,", s, &n));
    assert(!muse_dns_parse(",1.1.1.1", s, &n));
    assert(!muse_dns_parse("1.1.1.1 ", s, &n));
    assert(!muse_dns_parse("0.0.0.0", s, &n));
    assert(!muse_dns_parse("dns.google", s, &n));

    char out[MUSE_DNS_TEXT];
    s[0] = IP(1, 1, 1, 1);
    s[1] = IP(8, 8, 8, 8);
    muse_dns_format(s, 2, out, sizeof(out));
    assert(!strcmp(out, "1.1.1.1,8.8.8.8"));
    muse_dns_format(s, 0, out, sizeof(out));
    assert(!strcmp(out, ""));
}

static void keep(void)
{
    const uint32_t ROUTER = IP(192, 168, 0, 1), ROUTER2 = IP(205, 171, 2, 26);
    muse_dns_state_t st;
    uint32_t cfg[MUSE_DNS_MAX] = { IP(1, 1, 1, 1), IP(8, 8, 8, 8) };
    muse_dns_init(&st, cfg, 2);
    uint32_t want[3];

    /* A lease: the router's servers go in; ours go first, the router's last. */
    uint32_t cur[3] = { ROUTER, ROUTER2, 0 };
    assert(muse_dns_step(&st, cur, want));
    assert(want[0] == IP(1, 1, 1, 1) && want[1] == IP(8, 8, 8, 8) && want[2] == ROUTER);
    memcpy(cur, want, sizeof(cur));
    assert(!muse_dns_step(&st, cur, want));                         /* applied: nothing to do */

    /* A renewal quietly puts the router back first: put ours back. */
    cur[0] = ROUTER;
    cur[1] = ROUTER2;
    assert(muse_dns_step(&st, cur, want));
    assert(want[0] == IP(1, 1, 1, 1) && want[1] == IP(8, 8, 8, 8) && want[2] == ROUTER);

    /* No lease yet (nothing set): nothing to do. */
    muse_dns_init(&st, cfg, 2);
    uint32_t none[3] = { 0, 0, 0 };
    assert(!muse_dns_step(&st, none, want));

    /* One of ours: the router's as backup. */
    muse_dns_init(&st, cfg, 1);
    uint32_t lease[3] = { ROUTER, 0, 0 };
    assert(muse_dns_step(&st, lease, want));
    assert(want[0] == IP(1, 1, 1, 1) && want[1] == ROUTER && want[2] == 0);

    /* None of ours (">dns_servers="): leave DHCP's alone ... */
    muse_dns_init(&st, cfg, 0);
    assert(!muse_dns_step(&st, lease, want));

    /* ... and after an override, give the router's back. */
    muse_dns_init(&st, cfg, 2);
    uint32_t c2[3] = { ROUTER, 0, 0 };
    assert(muse_dns_step(&st, c2, want));
    memcpy(c2, want, sizeof(c2));
    muse_dns_set(&st, cfg, 0);
    assert(muse_dns_step(&st, c2, want));
    assert(want[0] == ROUTER && want[1] == 0 && want[2] == 0);
    memcpy(c2, want, sizeof(c2));
    assert(!muse_dns_step(&st, c2, want));

    /* A new list takes over at once. */
    uint32_t q[MUSE_DNS_MAX] = { IP(9, 9, 9, 9) };
    muse_dns_set(&st, q, 1);
    assert(muse_dns_step(&st, c2, want) && want[0] == IP(9, 9, 9, 9) && want[1] == ROUTER);
}

int main(void)
{
    parse();
    keep();
    return 0;
}
