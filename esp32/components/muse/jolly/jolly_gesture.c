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

#include "jolly/jolly_gesture.h"

enum { IDLE, DOWN, HOLDING, RELEASING, SWIPING };

void jolly_gesture_reset(jolly_gesture_t *g)
{
    g->state = IDLE;
    g->x0 = g->y0 = 0;
    g->t0 = g->t_up = 0;
    g->reason = JGR_RELEASED;
}

static int32_t dist(int32_t a, int32_t b)
{
    return a > b ? a - b : b - a;
}

jolly_gesture_ev_t jolly_gesture_feed(jolly_gesture_t *g, jolly_gesture_sample_t s, int32_t x, int32_t y,
                                      int64_t now_ms)
{
    if (s != JGS_PRESSED) {
        switch (g->state) {
        case DOWN:
            g->state = IDLE;
            return s == JGS_RELEASED ? JG_TAP : JG_NONE;   /* lost: the tileview took it */
        case HOLDING:
            /* Not over yet: a touch that blips up, or a press LVGL let go of
             * while the finger stays, mustn't cut the turn. jolly_gesture_tick
             * ends it once it has stayed up. */
            g->state = RELEASING;
            g->t_up = now_ms;
            g->reason = s == JGS_LOST ? JGR_LOST : JGR_RELEASED;
            return JG_NONE;
        case RELEASING:
            g->reason = s == JGS_LOST ? JGR_LOST : JGR_RELEASED;
            return JG_NONE;
        default:
            g->state = IDLE;   /* a swipe's end, or a stray */
            return JG_NONE;
        }
    }
    switch (g->state) {
    case IDLE:
        g->state = DOWN;
        g->x0 = x;
        g->y0 = y;
        g->t0 = now_ms;
        return JG_NONE;
    case DOWN:
        if (dist(x, g->x0) > JOLLY_MOVE_PX || dist(y, g->y0) > JOLLY_MOVE_PX) {
            g->state = SWIPING;
            return JG_SWIPE;
        }
        if (now_ms - g->t0 >= JOLLY_HOLD_MS) {
            g->state = HOLDING;
            return JG_HOLD_START;
        }
        return JG_NONE;
    case RELEASING:
        g->state = HOLDING;   /* down again: the talk goes on */
        return JG_NONE;
    default:
        return JG_NONE;       /* holding (never cancelled by moving), or swiping */
    }
}

jolly_gesture_ev_t jolly_gesture_tick(jolly_gesture_t *g, int64_t now_ms)
{
    if (g->state == RELEASING && now_ms - g->t_up >= JOLLY_RELEASE_MS) {
        g->state = IDLE;
        return JG_HOLD_END;
    }
    return JG_NONE;
}

bool jolly_gesture_holding(const jolly_gesture_t *g)
{
    return g->state == HOLDING || g->state == RELEASING;
}

bool jolly_gesture_down(const jolly_gesture_t *g)
{
    return g->state != IDLE;
}

jolly_gesture_reason_t jolly_gesture_end_reason(const jolly_gesture_t *g)
{
    return (jolly_gesture_reason_t)g->reason;
}
