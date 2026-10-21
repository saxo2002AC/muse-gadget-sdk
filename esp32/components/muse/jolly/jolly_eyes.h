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
 * Jolly's face, v5 "robot con expresión" (warroom-guardian
 * docs/mockup-cara-v5-fusion.html; numbers in jolly_face_spec.h): glowing
 * rounded-rect eyes with a highlight, a rim, a shadow and lids, bar brows, a
 * mouth per mood (lines, the talking pill, a ring, LED teeth) and extras
 * (blush, hearts, zzz, a sweat drop, sigh puffs, spirals, the alert's core
 * dot). It moves on springs: anticipation and overshoot on a mood change,
 * saccades with parallax and a head turn, squash-and-stretch blinks, and
 * breathing on the brows and mouth. Everything is computed per frame from
 * the clock (jolly_eyes_tick) and only what changed redraws; the glow goes
 * while an eye moves. Call all of these on the LVGL task.
 */

/* Creates the face on `parent`, a 480x320 tile, in the mockup's coordinates
 * (`y`, the old avatar's offset, is unused). A tap on
 * them calls on_pet after its heart; a hold (jolly_gesture.h) calls
 * on_hold(true), and on_hold(false) when it ends. Either may be NULL. */
void jolly_eyes_create(lv_obj_t *parent, int32_t y, void (*on_pet)(void), void (*on_hold)(bool down));

/* A finger is holding Jolly's face (push-to-talk). */
bool jolly_eyes_holding(void);

/* A finger is on Jolly's face at all. Safe to read from any task. */
bool jolly_eyes_touched(void);

/* The mood to show: it springs in, with anticipation (jolly_face_spec.h). */
void jolly_eyes_set_mood(jolly_mood_t m);

/* Holds the face on mood `m`, snapped to it with no motion (benches, the
 * simulator's contact sheet); JM_COUNT gives it back to jolly_eyes_set_mood. */
void jolly_eyes_show(jolly_mood_t m);

/* A pat in the middle of the eyes, as a tap there (benches, the simulator). */
void jolly_eyes_tap(void);

/* A heart at (x, y), in the face's own coordinates (the box below the status line). */
void jolly_eyes_pet(int32_t x, int32_t y);

/* Advances everything to now_ms (the board's monotonic clock), once a frame.
 * `level` is the reply's loudness (0..1) for the talking mouth. */
void jolly_eyes_tick(int64_t now_ms, float level);

/* The mood's colour, 0xRRGGBB (face v5's table). */
uint32_t jolly_mood_color(jolly_mood_t m);

#ifdef __cplusplus
}
#endif
