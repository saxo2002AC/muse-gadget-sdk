/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * jolly_fast_parse: the atajo's /v1/ask answer, strictly (contract v1.1,
 * Diana C13), and the breaker (2 failures in a row: skip the atajo 300 s).
 * argv[1] is tests/jolly.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jolly/jolly_fast_parse.h"

static size_t fixture(const char *dir, const char *name, char *buf, size_t cap)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/contract/atajo/%s", dir, name);
    FILE *f = fopen(path, "rb");
    assert(f);
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = '\0';
    return n;
}

static bool parse(const char *json, jolly_fast_answer_t *a)
{
    return jolly_fast_parse(json, strlen(json), a);
}

static void fixtures(const char *dir)
{
    char b[2048];
    jolly_fast_answer_t a;
    size_t n = fixture(dir, "ask.local.hora.json", b, sizeof(b));
    assert(jolly_fast_parse(b, n, &a));
    assert(a.local && !strcmp(a.intent, "hora") && !strcmp(a.text, "Son las tres y veinte de la tarde."));
    assert(a.stt_ms == 512 && a.total_ms == 561 && !a.transcript[0]);
    n = fixture(dir, "ask.local.estado.json", b, sizeof(b));
    assert(jolly_fast_parse(b, n, &a) && a.local && !strcmp(a.intent, "estado"));
    n = fixture(dir, "ask.forward.no_match.json", b, sizeof(b));
    assert(jolly_fast_parse(b, n, &a) && !a.local && !strcmp(a.reason, "no_match"));
    assert(!strcmp(a.transcript, "cuéntame un cuento de piratas"));
    n = fixture(dir, "ask.forward.no_speech.json", b, sizeof(b));
    assert(jolly_fast_parse(b, n, &a) && !a.local && !strcmp(a.reason, "no_speech"));
    n = fixture(dir, "ask.worstcase.json", b, sizeof(b));
    assert(jolly_fast_parse(b, n, &a) && strlen(a.transcript) == 400);
    n = fixture(dir, "ask.worstcase.local.json", b, sizeof(b));
    assert(jolly_fast_parse(b, n, &a) && strlen(a.text) == 280);
}

#define LOCAL(text) "{\"v\":1,\"action\":\"local\",\"intent\":\"hora\",\"text\":\"" text \
                    "\",\"reason\":\"\",\"transcript\":\"\",\"stt_ms\":1,\"total_ms\":2}"

static void strict(void)
{
    jolly_fast_answer_t a;
    assert(parse(LOCAL("Son las tres."), &a));
    /* Extra keys are ignored. */
    assert(parse("{\"v\":1,\"action\":\"local\",\"intent\":\"hora\",\"text\":\"Ya.\",\"reason\":\"\",\"transcript\":\"\","
                 "\"stt_ms\":1,\"total_ms\":2,\"extra\":[1,2]}", &a));
    /* Wrong version, unknown enums, missing or mistyped keys. */
    assert(!parse("{\"v\":2,\"action\":\"local\",\"intent\":\"hora\",\"text\":\"Ya.\",\"reason\":\"\",\"transcript\":\"\","
                  "\"stt_ms\":1,\"total_ms\":2}", &a));
    assert(!parse("{\"v\":1,\"action\":\"speak\",\"intent\":\"hora\",\"text\":\"Ya.\",\"reason\":\"\",\"transcript\":\"\","
                  "\"stt_ms\":1,\"total_ms\":2}", &a));
    assert(!parse("{\"v\":1,\"action\":\"local\",\"intent\":\"chiste\",\"text\":\"Ya.\",\"reason\":\"\",\"transcript\":\"\","
                  "\"stt_ms\":1,\"total_ms\":2}", &a));
    assert(!parse("{\"v\":1,\"action\":\"forward\",\"intent\":\"\",\"text\":\"\",\"reason\":\"because\",\"transcript\":\"\","
                  "\"stt_ms\":1,\"total_ms\":2}", &a));
    assert(!parse("{\"v\":1,\"action\":\"local\",\"intent\":\"hora\",\"text\":\"Ya.\",\"reason\":\"\",\"transcript\":\"\","
                  "\"stt_ms\":1}", &a));                                    /* total_ms missing */
    assert(!parse("{\"v\":\"1\",\"action\":\"local\",\"intent\":\"hora\",\"text\":\"Ya.\",\"reason\":\"\",\"transcript\":\"\","
                  "\"stt_ms\":1,\"total_ms\":2}", &a));                     /* v as a string */
    assert(!parse("{\"v\":1,\"action\":\"local\",\"intent\":\"hora\",\"text\":\"Ya.\",\"reason\":\"\",\"transcript\":\"\","
                  "\"stt_ms\":-1,\"total_ms\":2}", &a));                    /* out of range */
    assert(!parse("{\"v\":1,\"action\":\"local\",\"intent\":\"hora\",\"text\":\"Ya.\",\"reason\":\"\",\"transcript\":\"\","
                  "\"stt_ms\":1,\"total_ms\":60001}", &a));
    assert(!parse("[1]", &a) && !parse("", &a) && !parse("{", &a));
    /* local needs text; forward has none, and a reason. */
    assert(!parse(LOCAL(""), &a));
    assert(!parse("{\"v\":1,\"action\":\"forward\",\"intent\":\"\",\"text\":\"Hola\",\"reason\":\"no_match\","
                  "\"transcript\":\"\",\"stt_ms\":1,\"total_ms\":2}", &a));
    assert(!parse("{\"v\":1,\"action\":\"forward\",\"intent\":\"hora\",\"text\":\"\",\"reason\":\"no_match\","
                  "\"transcript\":\"\",\"stt_ms\":1,\"total_ms\":2}", &a));
    assert(!parse("{\"v\":1,\"action\":\"local\",\"intent\":\"hora\",\"text\":\"Ya.\",\"reason\":\"slow\","
                  "\"transcript\":\"\",\"stt_ms\":1,\"total_ms\":2}", &a));
    /* text: controls, C1, bidi and zero-width characters, bad UTF-8 (C13). */
    assert(!parse(LOCAL("Son\\nlas tres"), &a));
    assert(!parse(LOCAL("Son\\u0085las tres"), &a));                     /* C1 */
    assert(!parse(LOCAL("Son \xe2\x80\xae" "sert sal"), &a));               /* U+202E right-to-left override */
    assert(!parse(LOCAL("Son\xe2\x80\x8blas"), &a));                       /* U+200B zero width space */
    assert(!parse(LOCAL("Son \xef\xbb\xbflas"), &a));                       /* U+FEFF */
    assert(!parse(LOCAL("Son \xc3"), &a));                                  /* cut UTF-8 */
    assert(!parse(LOCAL("Son \xc0\xaf"), &a));                              /* overlong */
    assert(parse(LOCAL("Son las 3:45 pm, ¡ojo! Ñandú."), &a));             /* ordinary Spanish is fine */
    /* text over 280 bytes. */
    char big[1100];
    char t[300];
    memset(t, 'a', 281);
    t[281] = '\0';
    snprintf(big, sizeof(big), LOCAL("%s"), t);
    assert(!parse(big, &a));
    t[280] = '\0';
    snprintf(big, sizeof(big), LOCAL("%s"), t);
    assert(parse(big, &a));
    /* The body cap: 1024 bytes. */
    char huge[1200];
    memset(huge, ' ', sizeof(huge));
    memcpy(huge, LOCAL("Ya."), strlen(LOCAL("Ya.")));
    huge[1025] = '\0';
    assert(!jolly_fast_parse(huge, 1025, &a));
    assert(jolly_fast_parse(huge, 1024, &a));
}

static void breaker(void)
{
    jolly_fast_breaker_t b;
    jolly_fast_breaker_init(&b);
    assert(jolly_fast_breaker_closed(&b, 0));
    jolly_fast_breaker_result(&b, false, 1000);
    assert(jolly_fast_breaker_closed(&b, 1000));                         /* one failure: still asking */
    jolly_fast_breaker_result(&b, true, 2000);                           /* a 200 resets it */
    jolly_fast_breaker_result(&b, false, 3000);
    assert(jolly_fast_breaker_closed(&b, 3000));
    jolly_fast_breaker_result(&b, false, 4000);                          /* two in a row: open */
    assert(!jolly_fast_breaker_closed(&b, 4000));
    assert(!jolly_fast_breaker_closed(&b, 4000 + JOLLY_FAST_OPEN_MS - 1));
    assert(jolly_fast_breaker_closed(&b, 4000 + JOLLY_FAST_OPEN_MS));     /* 300 s later: try again */
    jolly_fast_breaker_result(&b, false, 400000);                         /* one more failure re-opens at once */
    assert(!jolly_fast_breaker_closed(&b, 400000));
    assert(JOLLY_FAST_OPEN_MS == 300000);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    fixtures(argv[1]);
    strict();
    breaker();
    return 0;
}
