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

#include "jolly/jolly_mood.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What the accelerometer says Jolly went through, from 100 Hz samples in
 * milli-g (jolly_imu.c feeds it the MPU-6050's), and the moods that asks for.
 * Pure, so tests/jolly/test_motion.c checks it on synthetic motion.
 *
 *   shake      the acceleration's spread over 450 mg for 400 ms -> sorprendido, mareado
 *   freefall   |a| under 300 mg for 120 ms                    -> asustado
 *   landing    an impact over 2 g within 1.5 s of a freefall  -> aliviado
 *   knock      a spike over 2.5 g lasting 30 ms or less, idle
 *              or touched, and not a landing or a shake      -> enojado
 *   face-down  gravity on -Z for 2 s                          -> durmiendo, until face-up
 */

typedef enum {
    JME_NONE, JME_SHAKE, JME_FREEFALL, JME_LAND, JME_KNOCK, JME_FACE_DOWN, JME_FACE_UP, JME_COUNT,
} jolly_motion_ev_t;

#define JOLLY_MOTION_WINDOW 50     /* samples in the shake window: 0.5 s at 100 Hz */

typedef struct {
    int16_t axis[3][JOLLY_MOTION_WINDOW];   /* x, y, z: the shake window */
    int n, at;
    int64_t shake_since, calm_since, ff_since, land_until, landed_at, down_since, up_since;
    bool shake_fired, ff_fired, face_down;
    int spike;                     /* consecutive samples over the knock threshold */
} jolly_motion_t;

void jolly_motion_init(jolly_motion_t *m);

/* One sample. `knock_gate`: Jolly is idle or being touched, so a knock counts. */
jolly_motion_ev_t jolly_motion_feed(jolly_motion_t *m, int16_t ax_mg, int16_t ay_mg, int16_t az_mg, int64_t now_ms,
                                    bool knock_gate);

#define JOLLY_SORPRENDIDO_SHAKE_MS 1000   /* a shake startles first (face v5)... */
#define JOLLY_MAREADO_MS 3000            /* ...then leaves Jolly dizzy */
#define JOLLY_ASUSTADO_MS 1500     /* at most: the landing ends it */
#define JOLLY_ALIVIADO_MS 2500
#define JOLLY_ENOJADO_MS 2000

typedef struct {
    jolly_mood_t mood;             /* the passing one */
    int64_t until;
    int64_t startled_until;        /* a shake's sorprendido, before its mareado */
    bool face_down;                /* under it: durmiendo */
} jolly_motion_mood_t;

void jolly_motion_mood_init(jolly_motion_mood_t *s);
void jolly_motion_mood_on(jolly_motion_mood_t *s, jolly_motion_ev_t ev, int64_t now_ms);

/* The mood the motion asks for now, or JM_TRANQUILO for none
 * (jolly_mood_in_t.motion). */
jolly_mood_t jolly_motion_mood_now(const jolly_motion_mood_t *s, int64_t now_ms);

#ifdef __cplusplus
}
#endif
