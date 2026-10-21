/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * The Guardián client's pure parts: the contract v1 parsers (strict, R15),
 * the ETag check and the link state (§1.8). argv[1] is tests/jolly.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jolly/jolly_glance.h"

static char *slurp(const char *dir, const char *name, size_t *len)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/contract/%s", dir, name);
    FILE *f = fopen(path, "rb");
    assert(f);
    static char buf[8192];
    *len = fread(buf, 1, sizeof(buf) - 1, f);
    buf[*len] = '\0';
    fclose(f);
    return buf;
}

static void parse_ok_fixture(const char *dir)
{
    size_t n;
    const char *j = slurp(dir, "glance.ok.json", &n);
    jolly_glance_t g;
    assert(jolly_glance_parse(j, n, &g));
    assert(g.ts == 1792634820 && g.seq == 18342 && g.age_s == 3);
    assert(g.level == JLV_OK && !g.quiet && !g.silenced && g.crit == 0 && g.warn == 0);
    assert(!strcmp(g.head, ""));
    assert(g.mac.cpu == 23 && g.mac.temp == 48 && g.mac.therm == JT_NORMAL && g.mac.mem == JLV_OK);
    assert(g.mac.swap_mb == 2662 && g.mac.swap_tot_mb == 4096 && g.mac.disk_pct == 5);
    assert(!strcmp(g.mac.macos, "26.7.1") && g.mac.updates == 0);
    assert(g.mac.backup_state == JS_OK && g.mac.backup_age_s == 3600);
    assert(g.mac.monitor_state == JS_OK && g.mac.monitor_age_s == 120);
    assert(g.apps.up == 25 && g.apps.total == 25 && g.apps.ndown == 0);
    assert(g.net.lat_ms == 14 && g.net.cf == 4 && g.net.cf_exp == 4 && !strcmp(g.net.ts_state, "up"));
    assert(g.net.has_speed && g.net.speed_down == 312 && g.net.speed_up == 41);
    assert(g.sec.level == JLV_OK && g.sec.ports == 15 && g.sec.ports_unreg == 3);
}

static void parse_crit_fixture(const char *dir)
{
    size_t n;
    const char *j = slurp(dir, "glance.crit.json", &n);
    jolly_glance_t g;
    assert(jolly_glance_parse(j, n, &g));
    assert(g.level == JLV_CRIT && g.crit >= 1 && g.head[0]);
    assert(g.apps.ndown >= 1 && g.apps.down[0].id[0]);
}

static void parse_nulls_fixture(const char *dir)
{
    size_t n;
    const char *j = slurp(dir, "glance.nulls.json", &n);
    jolly_glance_t g;
    assert(jolly_glance_parse(j, n, &g));
    assert(g.mac.temp == JOLLY_NONE && g.mac.updates == JOLLY_NONE);
    assert(g.mac.backup_state == JS_UNKNOWN && g.mac.backup_age_s == JOLLY_NONE);
    assert(g.mac.monitor_age_s == JOLLY_NONE && g.net.lat_ms == JOLLY_NONE && !g.net.has_speed);
}

static void parse_worstcase_utf8(const char *dir)
{
    size_t n;
    const char *j = slurp(dir, "glance.worstcase.json", &n);
    jolly_glance_t g;
    assert(jolly_glance_parse(j, n, &g));
    assert(g.ts == 4294967295LL && g.seq == 2147483647);
    assert(strlen(g.head) <= JOLLY_HEAD_MAX && g.head[0]);
    for (const unsigned char *p = (const unsigned char *)g.head; *p; p++) {
        assert(*p >= 0x20);
    }
    assert(g.apps.ndown == 3 && strlen(g.apps.down[0].id) == 24);
    assert(g.mac.cpu == 100 && g.mac.disk_pct == 100);
}

static void parse_rejects(void)
{
    jolly_glance_t g;
    const char *v2 = "{\"v\":2,\"ts\":1,\"seq\":1,\"age_s\":1,\"level\":\"ok\"}";
    assert(!jolly_glance_parse(v2, strlen(v2), &g));                         /* rejects_v2 */
    assert(!jolly_glance_parse("{", 1, &g));
    assert(!jolly_glance_parse("[]", 2, &g));
    static char big[JOLLY_GLANCE_MAX + 2];                                       /* oversize_rejected */
    memset(big, ' ', sizeof(big));
    big[0] = '{';
    big[sizeof(big) - 1] = '}';
    assert(!jolly_glance_parse(big, sizeof(big), &g));
}

/* wrong_types: a field of the wrong type sinks the whole document. */
static void parse_wrong_types(const char *dir)
{
    size_t n;
    const char *ok = slurp(dir, "glance.ok.json", &n);
    char doc[4096];
    struct { const char *from, *to; } cases[] = {
        { "\"cpu\": 23", "\"cpu\": \"23\"" },
        { "\"level\": \"ok\"", "\"level\": \"panic\"" },
        { "\"level\": \"ok\"", "\"level\": 1" },
        { "\"quiet\": false", "\"quiet\": 0" },
        { "\"head\": \"\"", "\"head\": 7" },
        { "\"down\": []", "\"down\": {}" },
        { "\"seq\": 18342", "\"seq\": null" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const char *at = strstr(ok, cases[i].from);
        assert(at);
        snprintf(doc, sizeof(doc), "%.*s%s%s", (int)(at - ok), ok, cases[i].to, at + strlen(cases[i].from));
        jolly_glance_t g;
        if (jolly_glance_parse(doc, strlen(doc), &g)) {
            fprintf(stderr, "accepted %s\n", cases[i].to);
            assert(0);
        }
    }
    /* Out of range integers are clamped, not trusted. */
    const char *at = strstr(ok, "\"cpu\": 23");
    snprintf(doc, sizeof(doc), "%.*s\"cpu\": 999999%s", (int)(at - ok), ok, at + strlen("\"cpu\": 23"));
    jolly_glance_t g;
    assert(jolly_glance_parse(doc, strlen(doc), &g) && g.mac.cpu == 100);
}

static void parse_lists(const char *dir)
{
    size_t n;
    jolly_apps_t a;
    const char *j = slurp(dir, "apps.json", &n);
    assert(jolly_apps_parse(j, n, &a));
    assert(a.n == 4 && !strcmp(a.apps[0].id, "scalp") && a.apps[0].st == JA_DEP);
    assert(a.apps[2].st == JA_DOWN && a.apps[2].port == 3020 && a.apps[3].st == JA_UNK);

    jolly_events_t e;
    j = slurp(dir, "events.json", &n);
    assert(jolly_events_parse(j, n, &e));
    assert(e.n == 3 && e.ev[0].sev == JLV_CRIT && e.ev[0].open && !e.ev[2].open);
    assert(strlen(e.ev[0].txt) <= JOLLY_TXT_MAX && e.ev[0].t == 1792634100);

    jolly_series_t s;
    j = slurp(dir, "series.10m.json", &n);
    assert(jolly_series_parse(j, n, &s));
    assert(s.n == 60 && s.has_tx && s.rx[58] == -1 && s.rx[59] == 9000 && s.step_s == 10);
    j = slurp(dir, "series.24h.json", &n);
    assert(jolly_series_parse(j, n, &s));
    assert(s.n == 24 && !s.has_tx && s.rx[0] == -1 && s.rx_peak_kbps == 46000);
    /* rx must be exactly 60 (10m) or 24 (24h) long. */
    const char *short_rx = "{\"v\":1,\"ts\":1,\"m\":\"net\",\"w\":\"24h\",\"step_s\":3600,\"t0\":1,\"rx\":[1,2],\"rx_peak_kbps\":3}";
    assert(!jolly_series_parse(short_rx, strlen(short_rx), &s));
}

static void etag(void)
{
    assert(jolly_etag_ok("\"g18342\""));
    assert(!jolly_etag_ok(""));
    assert(!jolly_etag_ok("\"g1\"\r\nX-Evil: 1"));                              /* etag_crlf_rejected */
    assert(!jolly_etag_ok("\"g1\"\n"));
    assert(!jolly_etag_ok("\"0123456789012345678901234567890123\""));          /* over 32 bytes */
    assert(!jolly_etag_ok("\"g\xc3\xa1\""));
}

static void link_rules(void)
{
    jolly_linkstate_t s;
    jolly_link_init(&s);
    assert(s.link == JL_UNREACHABLE);                                          /* nothing heard yet */
    jolly_link_on_result(&s, 200, true, 3, 1000);
    assert(s.link == JL_OK && s.last_ok_ms == 1000);
    jolly_link_on_result(&s, 0, true, 0, 4000);
    jolly_link_on_result(&s, 0, true, 0, 7000);
    assert(s.link == JL_OK);                                                   /* 2 fails: not yet */
    jolly_link_on_result(&s, 0, true, 0, 10000);
    assert(s.link == JL_UNREACHABLE);                                          /* unreachable_after_3_fails */
    jolly_link_on_result(&s, 304, true, 3, 11000);
    assert(s.link == JL_OK && s.last_ok_ms == 11000 && s.fails == 0);         /* 304_refreshes_last_ok */
    jolly_link_tick(&s, 26001);
    assert(s.link == JL_UNREACHABLE);                                          /* unreachable_after_15s */
    jolly_link_on_result(&s, 200, true, 61, 27000);
    assert(s.link == JL_STALE);                                                /* stale_age_over_60 */
    jolly_link_on_result(&s, 401, true, 0, 28000);
    assert(s.link == JL_UNAUTH && s.auth_status == 401);                       /* 401_unauth */
    jolly_link_on_result(&s, 403, true, 0, 29000);
    assert(s.link == JL_UNAUTH && s.auth_status == 403);
    jolly_link_on_result(&s, 200, false, 3, 30000);
    assert(s.link == JL_WIFI_DOWN);
    jolly_link_on_result(&s, 200, true, 3, 31000);
    jolly_link_on_result(&s, 503, true, 0, 40000);
    assert(s.link == JL_OK);                                                   /* warming up: holds <=15 s */
    jolly_link_on_result(&s, 503, true, 0, 47000);
    assert(s.link == JL_UNREACHABLE);
    assert(jolly_glance_is_new(18342, 1) && !jolly_glance_is_new(5, 5));       /* seq_reset_accepted */
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    parse_ok_fixture(argv[1]);
    parse_crit_fixture(argv[1]);
    parse_nulls_fixture(argv[1]);
    parse_worstcase_utf8(argv[1]);
    parse_rejects();
    parse_wrong_types(argv[1]);
    parse_lists(argv[1]);
    etag();
    link_rules();
    return 0;
}
