/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * jolly_gesture: a tap pets Jolly, a hold talks, a swipe changes page.
 */
#include <assert.h>

#include "jolly/jolly_gesture.h"

static jolly_gesture_t g;

static jolly_gesture_ev_t press(int x, int y, int64_t t) { return jolly_gesture_feed(&g, JGS_PRESSED, x, y, t); }
static jolly_gesture_ev_t lift(int x, int y, int64_t t) { return jolly_gesture_feed(&g, JGS_RELEASED, x, y, t); }
static jolly_gesture_ev_t lost(int64_t t) { return jolly_gesture_feed(&g, JGS_LOST, 0, 0, t); }

int main(void)
{
    /* A short tap. */
    jolly_gesture_reset(&g);
    assert(press(100, 100, 1000) == JG_NONE);
    assert(press(103, 98, 1100) == JG_NONE);
    assert(lift(103, 98, 1200) == JG_TAP);

    /* A hold: talk starts at the threshold, once, and ends on release. */
    jolly_gesture_reset(&g);
    assert(press(200, 150, 0) == JG_NONE);
    assert(press(205, 152, JOLLY_HOLD_MS - 1) == JG_NONE);
    assert(press(205, 152, JOLLY_HOLD_MS) == JG_HOLD_START);
    assert(press(240, 160, JOLLY_HOLD_MS + 500) == JG_NONE);   /* drifting while holding is fine */
    assert(lift(240, 160, 2000) == JG_HOLD_END);
    assert(jolly_gesture_feed(&g, JGS_PRESSED, 1, 1, 2001) == JG_NONE);   /* a new press starts over */

    /* A hold the tileview takes (a scroll after the threshold) still ends the talk. */
    jolly_gesture_reset(&g);
    press(200, 150, 0);
    assert(press(200, 150, 400) == JG_HOLD_START);
    assert(lost(600) == JG_HOLD_END);

    /* A swipe: moving more than JOLLY_MOVE_PX before the threshold cancels it. */
    jolly_gesture_reset(&g);
    press(300, 150, 0);
    assert(press(300 - JOLLY_MOVE_PX - 1, 150, 100) == JG_NONE);
    assert(press(150, 150, 500) == JG_NONE);                    /* no hold, however long */
    assert(lift(100, 150, 900) == JG_NONE);                     /* and no tap */
    jolly_gesture_reset(&g);
    press(300, 150, 0);
    assert(press(300, 150 + JOLLY_MOVE_PX + 1, 50) == JG_NONE); /* vertically too */
    assert(lift(300, 190, 100) == JG_NONE);

    /* Exactly JOLLY_MOVE_PX away is still a tap. */
    jolly_gesture_reset(&g);
    press(50, 50, 0);
    assert(lift(50 + JOLLY_MOVE_PX, 50, 100) == JG_TAP);

    /* The tileview taking the finger before the threshold: no tap. */
    jolly_gesture_reset(&g);
    press(50, 50, 0);
    assert(lost(80) == JG_NONE);

    /* A release with no press, or a press after a release, never confuses it. */
    jolly_gesture_reset(&g);
    assert(lift(1, 1, 10) == JG_NONE);
    assert(lost(10) == JG_NONE);
    assert(jolly_gesture_holding(&g) == false);
    press(1, 1, 20);
    press(1, 1, 20 + JOLLY_HOLD_MS);
    assert(jolly_gesture_holding(&g));
    return 0;
}
