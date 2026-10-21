/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * jolly_motion: the MPU-6050's gestures (shake, freefall and landing, knock,
 * face-down) and the moods they ask for, from synthetic 100 Hz samples.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "jolly/jolly_motion.h"

static jolly_motion_t m;
static int64_t t;
static int counts[JME_COUNT];

/* Feeds n samples 10 ms apart; counts the events. */
static void feed(int n, int ax, int ay, int az, bool gate)
{
    for (int i = 0; i < n; i++) {
        t += 10;
        counts[jolly_motion_feed(&m, (int16_t)ax, (int16_t)ay, (int16_t)az, t, gate)]++;
    }
}

static void fresh(void)
{
    jolly_motion_init(&m);
    t = 0;
    for (int i = 0; i < JME_COUNT; i++) {
        counts[i] = 0;
    }
    feed(100, 0, 0, 1000, true);   /* a second at rest, screen up */
}

static int events(void)
{
    int n = 0;
    for (int i = 1; i < JME_COUNT; i++) {
        n += counts[i];
    }
    return n;
}

static void rest_is_quiet(void)
{
    fresh();
    feed(500, 30, -20, 990, true);
    assert(events() == 0);
}

static void shake(void)
{
    fresh();
    for (int i = 0; i < 100; i++) {   /* a second of hard shaking */
        feed(1, (i % 2) ? 1500 : -1500, 0, 1000, true);
    }
    assert(counts[JME_SHAKE] == 1 && counts[JME_KNOCK] == 0);
    for (int i = 0; i < 200; i++) {   /* more of it: still once */
        feed(1, (i % 2) ? 1500 : -1500, 0, 1000, true);
    }
    assert(counts[JME_SHAKE] == 1);
    feed(200, 0, 0, 1000, true);      /* calm, then again */
    for (int i = 0; i < 100; i++) {
        feed(1, (i % 2) ? 1500 : -1500, 0, 1000, true);
    }
    assert(counts[JME_SHAKE] == 2);
}

static void freefall_and_landing(void)
{
    fresh();
    feed(20, 0, 0, 50, true);         /* 200 ms falling */
    assert(counts[JME_FREEFALL] == 1);
    feed(2, 0, 0, 3200, true);        /* the impact */
    feed(50, 0, 0, 1000, true);
    assert(counts[JME_LAND] == 1 && counts[JME_KNOCK] == 0);

    fresh();
    feed(8, 0, 0, 50, true);          /* 80 ms: a bump, not a fall */
    feed(50, 0, 0, 1000, true);
    assert(counts[JME_FREEFALL] == 0 && counts[JME_LAND] == 0);
}

static void knock(void)
{
    fresh();
    feed(1, 0, 0, 3000, true);        /* 10 ms, 3 g */
    feed(20, 0, 0, 1000, true);
    assert(counts[JME_KNOCK] == 1);

    fresh();
    feed(1, 0, 0, 3000, false);       /* not idle, not touched */
    feed(20, 0, 0, 1000, false);
    assert(counts[JME_KNOCK] == 0);

    fresh();
    feed(6, 0, 0, 3000, true);        /* 60 ms: a push, not a knock */
    feed(20, 0, 0, 1000, true);
    assert(counts[JME_KNOCK] == 0);
}

static void face_down(void)
{
    fresh();
    feed(190, 0, 0, -1000, true);     /* 1.9 s: not yet */
    assert(counts[JME_FACE_DOWN] == 0);
    feed(20, 0, 0, -1000, true);
    assert(counts[JME_FACE_DOWN] == 1);
    feed(200, 0, 0, -1000, true);
    assert(counts[JME_FACE_DOWN] == 1);
    feed(100, 0, 0, 1000, true);      /* turned back up */
    assert(counts[JME_FACE_UP] == 1);
}

static void moods(void)
{
    jolly_motion_mood_t s;
    jolly_motion_mood_init(&s);
    assert(jolly_motion_mood_now(&s, 0) == JM_TRANQUILO);   /* none */
    jolly_motion_mood_on(&s, JME_SHAKE, 1000);
    assert(jolly_motion_mood_now(&s, 1000) == JM_SORPRENDIDO);   /* face v5: startled first */
    assert(jolly_motion_mood_now(&s, 1000 + JOLLY_SORPRENDIDO_SHAKE_MS) == JM_MAREADO);
    assert(jolly_motion_mood_now(&s, 1000 + JOLLY_SORPRENDIDO_SHAKE_MS + JOLLY_MAREADO_MS - 1) == JM_MAREADO);
    assert(jolly_motion_mood_now(&s, 1000 + JOLLY_SORPRENDIDO_SHAKE_MS + JOLLY_MAREADO_MS) == JM_TRANQUILO);
    jolly_motion_mood_on(&s, JME_FREEFALL, 5000);
    assert(jolly_motion_mood_now(&s, 5100) == JM_ASUSTADO);
    jolly_motion_mood_on(&s, JME_LAND, 5300);
    assert(jolly_motion_mood_now(&s, 5400) == JM_ALIVIADO);
    assert(jolly_motion_mood_now(&s, 5300 + JOLLY_ALIVIADO_MS) == JM_TRANQUILO);
    jolly_motion_mood_on(&s, JME_FACE_DOWN, 9000);
    assert(jolly_motion_mood_now(&s, 20000) == JM_DURMIENDO);   /* for as long as it lies there */
    jolly_motion_mood_on(&s, JME_KNOCK, 20000);
    assert(jolly_motion_mood_now(&s, 20100) == JM_ENOJADO);     /* a knock wakes it up, cross */
    assert(jolly_motion_mood_now(&s, 20000 + JOLLY_ENOJADO_MS) == JM_DURMIENDO);
    jolly_motion_mood_on(&s, JME_FACE_UP, 30000);
    assert(jolly_motion_mood_now(&s, 30000) == JM_TRANQUILO);
    /* Face v5 flashes the screen on the landing and the knock themselves,
     * whatever mood shows: one count each, nothing for the others. */
    assert(s.flashes == 2);
    jolly_motion_mood_on(&s, JME_SHAKE, 31000);
    jolly_motion_mood_on(&s, JME_FREEFALL, 32000);
    assert(s.flashes == 2);
    jolly_motion_mood_on(&s, JME_KNOCK, 33000);
    assert(s.flashes == 3);
}

static void arbiter(void)
{
    jolly_mood_in_t in = { .voice = MUSE_MODE_IDLE, .link = JL_OK, .level = JLV_OK, .sec_level = JLV_OK,
                           .now_ms = 1000, .motion = JM_MAREADO };
    assert(jolly_mood_pick(&in) == JM_MAREADO);
    in.level = JLV_CRIT;                        /* a current crit wins over play */
    assert(jolly_mood_pick(&in) == JM_ALERTA);
    in.link = JL_UNREACHABLE;                   /* a stale one doesn't */
    assert(jolly_mood_pick(&in) == JM_MAREADO);
    in.level = JLV_OK;
    in.sec_level = JLV_WARN;
    in.quiet = true;
    assert(jolly_mood_pick(&in) == JM_MAREADO); /* beats no link, espía, quiet */
    in.voice = MUSE_MODE_SPEAKING;              /* voice still first */
    assert(jolly_mood_pick(&in) == JM_HABLANDO);
    in.voice = MUSE_MODE_IDLE;
    in.pet_until_ms = 2000;                     /* and a pat */
    assert(jolly_mood_pick(&in) == JM_CARINO);
}

int main(void)
{
    rest_is_quiet();
    shake();
    freefall_and_landing();
    knock();
    face_down();
    moods();
    arbiter();
    return 0;
}
