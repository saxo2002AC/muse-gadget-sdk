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
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Touches on Jolly's face: a tap pets it, a hold (JOLLY_HOLD_MS still) is
 * push-to-talk until the finger lifts, and a finger that moves more than
 * JOLLY_MOVE_PX first is a swipe to another page, so neither. Once a talk
 * has started, nothing but a lasting release ends it: moving never does, and
 * a release or a lost press only does after JOLLY_RELEASE_MS with no touch
 * back (turns arrived cut off when a blip or the tileview ended them). Pure,
 * so tests/jolly/test_gesture.c checks it.
 */

#define JOLLY_HOLD_MS 350      /* longer than a pat */
#define JOLLY_MOVE_PX 40       /* further than a pat, or a held finger, wobbles */
#define JOLLY_RELEASE_MS 150   /* up this long: the talk is over */

typedef enum {
    JGS_PRESSED,   /* down, at (x, y): every touch read while it lasts */
    JGS_RELEASED,  /* lifted */
    JGS_LOST,      /* taken away, as by the tileview starting a scroll */
} jolly_gesture_sample_t;

typedef enum { JG_NONE, JG_TAP, JG_HOLD_START, JG_HOLD_END, JG_SWIPE } jolly_gesture_ev_t;

typedef enum { JGR_RELEASED, JGR_LOST } jolly_gesture_reason_t;   /* how a talk ended */

typedef struct {
    int state;
    int32_t x0, y0;
    int64_t t0, t_up;
    int reason;
} jolly_gesture_t;

void jolly_gesture_reset(jolly_gesture_t *g);
jolly_gesture_ev_t jolly_gesture_feed(jolly_gesture_t *g, jolly_gesture_sample_t s, int32_t x, int32_t y,
                                      int64_t now_ms);

/* Once a frame: JG_HOLD_END when a released talk has stayed released. */
jolly_gesture_ev_t jolly_gesture_tick(jolly_gesture_t *g, int64_t now_ms);

/* Talking (held, or released for less than JOLLY_RELEASE_MS). */
bool jolly_gesture_holding(const jolly_gesture_t *g);

/* Any touch under way. */
bool jolly_gesture_down(const jolly_gesture_t *g);

jolly_gesture_reason_t jolly_gesture_end_reason(const jolly_gesture_t *g);

#ifdef __cplusplus
}
#endif
