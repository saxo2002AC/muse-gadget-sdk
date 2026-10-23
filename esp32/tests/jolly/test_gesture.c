/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * jolly_gesture: a tap pets Jolly, a hold talks, a swipe changes page. A
 * held talk survives a wiggle and a lost press; only a release that stays
 * released for JOLLY_RELEASE_MS ends it (Andrés's turns arrived cut off).
 */
#include <assert.h>

#include "jolly/jolly_gesture.h"

static jolly_gesture_t g;

static jolly_gesture_ev_t press(int x, int y, int64_t t) { return jolly_gesture_feed(&g, JGS_PRESSED, x, y, t); }
static jolly_gesture_ev_t lift(int x, int y, int64_t t) { return jolly_gesture_feed(&g, JGS_RELEASED, x, y, t); }
static jolly_gesture_ev_t lost(int64_t t) { return jolly_gesture_feed(&g, JGS_LOST, 0, 0, t); }
static jolly_gesture_ev_t tick(int64_t t) { return jolly_gesture_tick(&g, t); }

/* Holds from (x, y) at t0 until the threshold: talking. */
static void hold(int x, int y, int64_t t0)
{
    jolly_gesture_reset(&g);
    assert(press(x, y, t0) == JG_NONE);
    assert(press(x, y, t0 + JOLLY_HOLD_MS - 1) == JG_NONE);
    assert(press(x, y, t0 + JOLLY_HOLD_MS) == JG_HOLD_START);
    assert(jolly_gesture_holding(&g));
}

static void taps(void)
{
    jolly_gesture_reset(&g);
    assert(press(100, 100, 1000) == JG_NONE);
    assert(press(103, 98, 1100) == JG_NONE);
    assert(lift(103, 98, 1200) == JG_TAP);
    assert(tick(5000) == JG_NONE);

    /* A pat that wobbles up to JOLLY_MOVE_PX is still a pat. */
    jolly_gesture_reset(&g);
    press(50, 50, 0);
    assert(press(50 + JOLLY_MOVE_PX, 50, 50) == JG_NONE);
    assert(lift(50 + JOLLY_MOVE_PX, 50, 100) == JG_TAP);

    /* The tileview taking the finger before the threshold: no tap. */
    jolly_gesture_reset(&g);
    press(50, 50, 0);
    assert(lost(80) == JG_NONE);
}

static void holds(void)
{
    /* A hold ends once the finger has been up for JOLLY_RELEASE_MS. */
    hold(200, 150, 0);
    assert(lift(200, 150, 2000) == JG_NONE);
    assert(jolly_gesture_holding(&g));                       /* still talking while it settles */
    assert(tick(2000 + JOLLY_RELEASE_MS - 1) == JG_NONE);
    assert(tick(2000 + JOLLY_RELEASE_MS) == JG_HOLD_END);
    assert(jolly_gesture_end_reason(&g) == JGR_RELEASED);
    assert(!jolly_gesture_holding(&g));
    assert(tick(9000) == JG_NONE);                           /* once */
    assert(press(1, 1, 9001) == JG_NONE);                    /* a new press starts over */

    /* A blip: the touch reads up for a moment and down again. The talk goes on. */
    hold(200, 150, 0);
    assert(lift(200, 150, 1000) == JG_NONE);
    assert(tick(1000 + JOLLY_RELEASE_MS / 2) == JG_NONE);
    assert(press(201, 151, 1000 + JOLLY_RELEASE_MS / 2 + 10) == JG_NONE);
    assert(tick(1000 + 5 * JOLLY_RELEASE_MS) == JG_NONE);
    assert(jolly_gesture_holding(&g));
    assert(lift(201, 151, 3000) == JG_NONE);
    assert(tick(3000 + JOLLY_RELEASE_MS) == JG_HOLD_END);

    /* Wiggling far while held never cancels it. */
    hold(200, 150, 0);
    assert(press(200 + 3 * JOLLY_MOVE_PX, 150 - 2 * JOLLY_MOVE_PX, 900) == JG_NONE);
    assert(jolly_gesture_holding(&g));

    /* A lost press while held (the finger's still down; the caller keeps
     * feeding the touch it reads): the talk goes on. */
    hold(200, 150, 0);
    assert(lost(600) == JG_NONE);
    assert(press(200, 150, 620) == JG_NONE);
    assert(tick(600 + 4 * JOLLY_RELEASE_MS) == JG_NONE);
    assert(jolly_gesture_holding(&g));
    /* ... and once the finger's really gone, it ends, saying how. */
    assert(lost(2000) == JG_NONE);
    assert(tick(2000 + JOLLY_RELEASE_MS) == JG_HOLD_END);
    assert(jolly_gesture_end_reason(&g) == JGR_LOST);
}

static void swipes(void)
{
    /* Moving more than JOLLY_MOVE_PX before the threshold is a swipe: said
     * once, then no hold however long, and no tap. */
    jolly_gesture_reset(&g);
    press(300, 150, 0);
    assert(press(300 - JOLLY_MOVE_PX - 1, 150, 100) == JG_SWIPE);
    assert(press(150, 150, 500) == JG_NONE);
    assert(lift(100, 150, 900) == JG_NONE);
    assert(tick(2000) == JG_NONE);
    jolly_gesture_reset(&g);
    press(300, 150, 0);
    assert(press(300, 150 + JOLLY_MOVE_PX + 1, 50) == JG_SWIPE);   /* vertically too */
    assert(lost(100) == JG_NONE);
    assert(JOLLY_MOVE_PX >= 40);                                  /* a held finger wiggles */
}

static void strays(void)
{
    jolly_gesture_reset(&g);
    assert(lift(1, 1, 10) == JG_NONE);
    assert(lost(10) == JG_NONE);
    assert(tick(10) == JG_NONE);
    assert(jolly_gesture_holding(&g) == false);
    press(1, 1, 20);
    press(1, 1, 20 + JOLLY_HOLD_MS);
    assert(jolly_gesture_holding(&g));
    assert(jolly_gesture_down(&g));
}

int main(void)
{
    taps();
    holds();
    swipes();
    strays();
    return 0;
}
