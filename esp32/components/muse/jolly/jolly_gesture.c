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

enum { IDLE, DOWN, HOLDING, SWIPING };

void jolly_gesture_reset(jolly_gesture_t *g)
{
    g->state = IDLE;
    g->x0 = g->y0 = 0;
    g->t0 = 0;
}

static int32_t dist(int32_t a, int32_t b)
{
    return a > b ? a - b : b - a;
}

jolly_gesture_ev_t jolly_gesture_feed(jolly_gesture_t *g, jolly_gesture_sample_t s, int32_t x, int32_t y,
                                      int64_t now_ms)
{
    if (s != JGS_PRESSED) {
        int was = g->state;
        g->state = IDLE;
        if (was == HOLDING) {
            return JG_HOLD_END;   /* lifted or taken: the talk ends either way */
        }
        return was == DOWN && s == JGS_RELEASED ? JG_TAP : JG_NONE;
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
            return JG_NONE;
        }
        if (now_ms - g->t0 >= JOLLY_HOLD_MS) {
            g->state = HOLDING;
            return JG_HOLD_START;
        }
        return JG_NONE;
    default:
        return JG_NONE;   /* holding, or swiping: nothing new until it lifts */
    }
}

bool jolly_gesture_holding(const jolly_gesture_t *g)
{
    return g->state == HOLDING;
}
