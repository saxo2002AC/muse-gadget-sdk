/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * muse_speech_next: where the next piece of a reply to speak ends, so speech
 * starts on the first whole sentence while the rest is still arriving, and a
 * long story goes as several requests instead of being cut.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "muse_speech_split.h"

static size_t next(const char *t, size_t from, bool final, size_t min, size_t max)
{
    return muse_speech_next(t, from, strlen(t), final, min, max);
}

static void sentences(void)
{
    const char *t = "Perfecto, sí te la creo. Había una vez una niña";
    /* Still arriving: the first whole sentence, with its space. */
    size_t e = next(t, 0, false, 10, 600);
    assert(e == strlen("Perfecto, sí te la creo. "));
    /* Nothing more is whole yet. */
    assert(next(t, e, false, 10, 600) == e);
    /* Done: the rest. */
    assert(next(t, e, true, 10, 600) == strlen(t));
    assert(next(t, strlen(t), true, 10, 600) == strlen(t));        /* nothing left */

    /* A sentence end needs what follows it: "3." may be "3.5". */
    assert(next("Son las 3.", 0, false, 1, 600) == 0);
    assert(next("Cuesta 3.5 dólares. Y", 0, false, 1, 600) == strlen("Cuesta 3.5 dólares. "));
    /* ... but the end of a finished message is an end. */
    assert(next("Son las 3.", 0, true, 1, 600) == strlen("Son las 3."));

    /* Too short to be worth a request: wait for the next sentence. */
    t = "Sí. Claro que sí, te cuento una historia. Era";
    assert(next(t, 0, false, 20, 600) == strlen("Sí. Claro que sí, te cuento una historia. "));

    /* ! ? … and line breaks end sentences; closing quotes stay with them. */
    assert(next("¡Hola! Qué", 0, false, 1, 600) == strlen("¡Hola! "));
    assert(next("¿Listo? Va", 0, false, 1, 600) == strlen("¿Listo? "));
    assert(next("Y entonces\xE2\x80\xA6 nada", 0, false, 1, 600) == strlen("Y entonces\xE2\x80\xA6 "));
    assert(next("Primera línea\nSegunda", 0, false, 1, 600) == strlen("Primera línea\n"));
    assert(next("Dijo \"adiós.\" Y se fue", 0, false, 1, 600) == strlen("Dijo \"adiós.\" "));
}

static void long_text(void)
{
    /* No sentence end within max: cut at the last space before it. */
    char t[400];
    t[0] = '\0';
    for (int i = 0; i < 40; i++) {
        strcat(t, "palabra ");                                   /* 8 bytes each */
    }
    size_t e = next(t, 0, false, 10, 100);
    assert(e > 0 && e <= 100 && t[e - 1] == ' ');
    /* Final text longer than max: in pieces, each at most max, covering all. */
    size_t at = 0, pieces = 0;
    while (at < strlen(t)) {
        size_t n = next(t, at, true, 10, 100);
        assert(n > at && n - at <= 100);
        at = n;
        pieces++;
    }
    assert(at == strlen(t) && pieces >= 4);
    /* A word longer than max: cut at max anyway, on a UTF-8 boundary. */
    char w[300];
    memset(w, 'a', 250);
    memcpy(w + 99, "\xC3\xA9", 2);                               /* é across byte 100 */
    w[250] = '\0';
    e = next(w, 0, true, 10, 100);
    assert(e == 99);                                             /* not inside the é */
}

int main(void)
{
    sentences();
    long_text();
    return 0;
}
