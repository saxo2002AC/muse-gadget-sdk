/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * jolly_text: the Spanish strings the status pages show.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "jolly/jolly_text.h"

#define EQ(expr, want)                                                              \
    do {                                                                            \
        char b[64];                                                                 \
        expr;                                                                       \
        if (strcmp(b, want)) {                                                      \
            fprintf(stderr, "%s: got \"%s\", want \"%s\"\n", #expr, b, want);       \
            assert(0);                                                              \
        }                                                                           \
    } while (0)

int main(void)
{
    EQ(jolly_fmt_ago(3, b, sizeof(b)), "hace 3 s");
    EQ(jolly_fmt_ago(59, b, sizeof(b)), "hace 59 s");
    EQ(jolly_fmt_ago(120, b, sizeof(b)), "hace 2 min");
    EQ(jolly_fmt_ago(7200, b, sizeof(b)), "hace 2 h");
    EQ(jolly_fmt_ago(86400, b, sizeof(b)), "hace 1 día");
    EQ(jolly_fmt_ago(864000, b, sizeof(b)), "hace 10 días");
    EQ(jolly_fmt_ago(-5, b, sizeof(b)), "ahora");
    EQ(jolly_fmt_ago(JOLLY_NONE, b, sizeof(b)), "—");

    EQ(jolly_fmt_tenths(8400, b, sizeof(b)), "8.4");      /* kbps -> Mb/s, MB -> GB */
    EQ(jolly_fmt_tenths(1200, b, sizeof(b)), "1.2");
    EQ(jolly_fmt_tenths(46000, b, sizeof(b)), "46.0");
    EQ(jolly_fmt_tenths(0, b, sizeof(b)), "0.0");
    EQ(jolly_fmt_tenths(-1, b, sizeof(b)), "—");
    EQ(jolly_fmt_gib(2662, b, sizeof(b)), "2.6");         /* MB of swap as GB, as macOS shows it */
    EQ(jolly_fmt_gib(4096, b, sizeof(b)), "4.0");
    EQ(jolly_fmt_gib(-1, b, sizeof(b)), "—");

    jolly_glance_t g;
    memset(&g, 0, sizeof(g));
    jolly_tone_t tone;
    EQ(jolly_fmt_chip(JL_OK, 0, &g, b, sizeof(b), &tone), "Mac Mini OK");
    assert(tone == JTONE_OK);
    g.warn = 2;
    EQ(jolly_fmt_chip(JL_OK, 0, &g, b, sizeof(b), &tone), "2 avisos");
    assert(tone == JTONE_WARN);
    g.warn = 1;
    EQ(jolly_fmt_chip(JL_OK, 0, &g, b, sizeof(b), &tone), "1 aviso");
    g.crit = 1;
    EQ(jolly_fmt_chip(JL_OK, 0, &g, b, sizeof(b), &tone), "1 crítica");
    assert(tone == JTONE_CRIT);
    g.crit = 3;
    EQ(jolly_fmt_chip(JL_OK, 0, &g, b, sizeof(b), &tone), "3 críticas");
    EQ(jolly_fmt_chip(JL_OK, 0, NULL, b, sizeof(b), &tone), "Sin datos");
    assert(tone == JTONE_STALE);
    EQ(jolly_fmt_chip(JL_WIFI_DOWN, 0, &g, b, sizeof(b), &tone), "Sin WiFi");
    EQ(jolly_fmt_chip(JL_UNREACHABLE, 0, &g, b, sizeof(b), &tone), "Sin conexión con el Mac Mini");
    EQ(jolly_fmt_chip(JL_UNAUTH, 401, &g, b, sizeof(b), &tone), "Clave inválida");
    EQ(jolly_fmt_chip(JL_UNAUTH, 403, &g, b, sizeof(b), &tone), "IP no permitida");
    EQ(jolly_fmt_chip(JL_STALE, 0, &g, b, sizeof(b), &tone), "Sin datos");
    assert(tone == JTONE_STALE);

    assert(!strcmp(jolly_word_level(JLV_OK), "Normal") && !strcmp(jolly_word_level(JLV_CRIT), "Crítica"));
    assert(!strcmp(jolly_word_therm(JT_NORMAL), "normal") && !strcmp(jolly_word_therm(JT_CRITICO), "crítica"));
    assert(!strcmp(jolly_word_sev(JLV_CRIT), "crítico") && !strcmp(jolly_word_sev(JLV_WARN), "aviso") &&
           !strcmp(jolly_word_sev(JLV_OK), "info"));
    return 0;
}
