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
 * Face v5 ("robot con expresión", warroom-guardian/docs/mockup-cara-v5-fusion.html),
 * as numbers: each mood's colour, eyes, mouth, effects and spring targets, the
 * talking visemes, the springs, the blink curve, the head turn and the mouth
 * lines. Pure, so tests/jolly/test_face.c checks it against the mockup;
 * jolly_eyes.c draws it.
 *
 * Geometry is the mockup's 480x320 screen: eyes 112x128 centred at x 170 and
 * 310, y 150; brows 84x9 at y 64; the mouth centred at (240, 254).
 */

typedef enum {
    JFE_OPEN,          /* the glowing rounded rect */
    JFE_CLOSED_UP,     /* a ^ arc: gusto, a wink */
    JFE_CLOSED_DOWN,   /* a ‿ arc: asleep */
    JFE_SPIRAL,        /* three spinning arcs: dizzy */
} jolly_face_eye_t;

typedef enum {
    JFM_SMILE, JFM_BIG, JFM_SMIRK, JFM_SIDE, JFM_FLAT, JFM_ZIG, JFM_WAVE,   /* lines */
    JFM_PILL,          /* the talking pill (hablando, feliz) */
    JFM_RING,          /* an O, scaled by `os` */
    JFM_GRILL,         /* LED teeth */
} jolly_face_mouth_t;

/* Extras, a mood's bit mask. */
enum {
    JFX_TONGUE = 1 << 0,       /* pixel tongue in the pill */
    JFX_HEARTS = 1 << 1,
    JFX_ZZZ = 1 << 2,
    JFX_ALARM = 1 << 3,        /* the screen's border pulses */
    JFX_SHAKE = 1 << 4,        /* the face jitters (alerta) */
    JFX_WOBBLE = 1 << 5,       /* sways side to side (mareado) */
    JFX_RAGE = 1 << 6,         /* shudders (enojado) */
    JFX_WAG_TONGUE = 1 << 7,   /* a tongue out the mouth's corner (guiño) */
    JFX_DROP = 1 << 8,         /* a sweat drop */
    JFX_TREMBLE = 1 << 9,
    JFX_ROUND_EYES = 1 << 10,
    JFX_PUFF = 1 << 11,        /* sigh puffs */
};

typedef struct {
    uint32_t color;            /* 0xRRGGBB */
    uint32_t blush_color;
    uint8_t eye_l, eye_r;      /* jolly_face_eye_t */
    uint8_t mouth;             /* jolly_face_mouth_t */
    bool blinks;               /* blinks on its own */
    uint16_t fx;               /* JFX_* */
    /* Spring targets, the mockup's BASE plus the mood's own: eye scale, spread,
     * gaze offset, lids (top cover, top tilt in degrees, bottom cover, the
     * curved happy lid's height), the core dot, brow y and angle, blush, the
     * ring's scale. */
    float esx, esy, spr, gox, goy;
    float ltL, ltR, lrL, lrR, lbL, lbR, lhL, lhR, core;
    float byL, byR, brL, brR, blush, os;
    /* Saccades: range x, y; the chance of a big jump; the wait between, ms.
     * gaze_bx 0: the eyes don't wander, they look ahead. */
    int16_t gaze_bx, gaze_by;
    float gaze_big;
    uint16_t gaze_min_ms, gaze_max_ms;
} jolly_face_mood_t;

/* The mood's face; out of range reads as tranquilo. */
const jolly_face_mood_t *jolly_face_mood(jolly_mood_t m);

/* The mood's name, ASCII Spanish ("carino", "guino"), and back: JM_COUNT
 * for a name that isn't one (the simulator's mood= key, the >imu line). */
const char *jolly_face_name(jolly_mood_t m);
jolly_mood_t jolly_face_named(const char *name);

#define JOLLY_FACE_BLUSH 0xFF8FB8
#define JOLLY_FACE_TONGUE 0xFF6F91

/* Talking: the pill's shape from the reply's level. */
typedef enum { JV_CERRADA, JV_CHICA, JV_MEDIA, JV_ANCHA, JV_O } jolly_viseme_kind_t;

typedef struct {
    jolly_viseme_kind_t kind;
    float mx, my;              /* the pill's scale */
} jolly_viseme_t;

/* `round`: this syllable may be an O (only when it's loud enough). */
jolly_viseme_t jolly_face_viseme(float level, bool round);

/* The mockup's springs: v = (v + (t - x) k) d; x += v. */
#define JOLLY_SPRING_K 0.11f
#define JOLLY_SPRING_K_TALK 0.35f   /* the pill, while talking */
#define JOLLY_SPRING_DAMP 0.74f
#define JOLLY_GAZE_K 0.22f
#define JOLLY_GAZE_DAMP 0.62f

void jolly_spring_step(float *x, float *v, float target, float k, float damp);

/* How shut a blink is (0 open, 1 shut) at progress p (0..1): closes by 45 %,
 * holds to 50 % (65 % when slow), opens again. */
float jolly_face_blink(float p, bool slow);

/* Head turn: an eye's width factor for gaze x `gx` (px); side 0 left, 1 right.
 * The eye the face turns toward widens, the other narrows, at most 16 %. */
float jolly_face_turn(float gx, int side);

/* A line mouth's points around the mouth's centre, x then y; returns how many
 * (0 for mouths that aren't lines). `max` is in points. */
#define JOLLY_MOUTH_POINTS 13
int jolly_face_mouth_line(jolly_face_mouth_t kind, int16_t *xy, int max);

#ifdef __cplusplus
}
#endif
