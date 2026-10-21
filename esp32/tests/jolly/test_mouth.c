/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * jolly_mouth: the mouth for each mood, and its opening while speaking.
 */
#include <assert.h>

#include "jolly/jolly_mouth.h"

int main(void)
{
    jolly_mouth_t m = jolly_mouth_for(JM_TRANQUILO);   /* a small soft smile */
    assert(m.kind == JMOUTH_SMILE && !m.jitter && !m.bubble);
    int small = m.w;
    m = jolly_mouth_for(JM_FELIZ);                     /* a bigger smile */
    assert(m.kind == JMOUTH_SMILE && m.w > small);
    m = jolly_mouth_for(JM_CARINO);
    assert(m.kind == JMOUTH_SMILE && m.w > small);
    m = jolly_mouth_for(JM_HABLANDO);                  /* lip-sync */
    assert(m.kind == JMOUTH_TALK);
    m = jolly_mouth_for(JM_ESCUCHANDO);                /* a small "o" */
    assert(m.kind == JMOUTH_O && !m.jitter);
    int o = m.w;
    m = jolly_mouth_for(JM_ALERTA);                    /* a big "O", trembling */
    assert(m.kind == JMOUTH_O && m.w > o && m.jitter);
    m = jolly_mouth_for(JM_PENSANDO);                  /* a flat line, to the side */
    assert(m.kind == JMOUTH_LINE && m.dx != 0);
    m = jolly_mouth_for(JM_DURMIENDO);                 /* a tiny line and a bubble */
    assert(m.kind == JMOUTH_LINE && m.bubble);
    int tiny = m.w;
    m = jolly_mouth_for(JM_DESCONECTADO);              /* a flat line */
    assert(m.kind == JMOUTH_LINE && m.dx == 0 && m.w > tiny && !m.bubble);
    m = jolly_mouth_for(JM_ESPIA);
    assert(m.kind == JMOUTH_LINE);
    m = jolly_mouth_for(JM_COUNT);                     /* out of range: as tranquilo */
    assert(m.kind == JMOUTH_SMILE && m.w == small);

    /* Speaking: the opening follows the reply's level when there is one... */
    assert(jolly_mouth_talk_h(1.0f, 0) == JOLLY_MOUTH_TALK_MAX);
    assert(jolly_mouth_talk_h(2.0f, 0) == JOLLY_MOUTH_TALK_MAX);
    int half = jolly_mouth_talk_h(0.5f, 0);
    assert(half > JOLLY_MOUTH_TALK_MIN && half < JOLLY_MOUTH_TALK_MAX);
    assert(jolly_mouth_talk_h(0.25f, 0) < half);
    /* ...and with none (silence between words, or no level), a natural pattern
     * that opens and closes, always within range. */
    int lo = 1000, hi = -1;
    for (int64_t t = 0; t < 4000; t += 20) {
        int h = jolly_mouth_talk_h(0.0f, t);
        assert(h >= JOLLY_MOUTH_TALK_MIN && h <= JOLLY_MOUTH_TALK_MAX);
        lo = h < lo ? h : lo;
        hi = h > hi ? h : hi;
    }
    assert(hi - lo >= 8);
    return 0;
}
