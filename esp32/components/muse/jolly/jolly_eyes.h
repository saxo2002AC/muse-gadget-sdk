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

#include <stdint.h>

#include "lvgl.h"
#include "jolly/jolly_mood.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Jolly's eyes and mouth: two rounded rectangles (style B, "robot") with lids, in the
 * mood's colour, that wander (saccades), blink (single, double or slow, with
 * squash and stretch) and take a new mood behind a blink. Cariño sways and
 * sends hearts up; a pat adds one where it landed. Everything is computed
 * per frame from the clock (jolly_eyes_tick), so only the eyes' area redraws.
 * Call all of these on the LVGL task.
 */

/* Creates the eyes centred in `parent`, `y` px below its centre. A tap on
 * them calls on_pet after its heart; a hold (jolly_gesture.h) calls
 * on_hold(true), and on_hold(false) when it ends. Either may be NULL. */
void jolly_eyes_create(lv_obj_t *parent, int32_t y, void (*on_pet)(void), void (*on_hold)(bool down));

/* A finger is holding Jolly's face (push-to-talk). */
bool jolly_eyes_holding(void);

/* The mood to show: applied behind the next blink, or now for moods that
 * don't blink. */
void jolly_eyes_set_mood(jolly_mood_t m);

/* A pat in the middle of the eyes, as a tap there (benches, the simulator). */
void jolly_eyes_tap(void);

/* A heart at (x, y), in the eyes' own coordinates. */
void jolly_eyes_pet(int32_t x, int32_t y);

/* Advances everything to now_ms (the board's monotonic clock), once a frame.
 * `level` is the reply's loudness (0..1) for the talking mouth. */
void jolly_eyes_tick(int64_t now_ms, float level);

/* The mood's colour, 0xRRGGBB (the guardian plan's table). */
uint32_t jolly_mood_color(jolly_mood_t m);

#ifdef __cplusplus
}
#endif
