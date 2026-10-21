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

#include "jolly/jolly_face_spec.h"

#include <stddef.h>
#include <string.h>

#define O JFE_OPEN
#define U JFE_CLOSED_UP
#define D JFE_CLOSED_DOWN
#define S JFE_SPIRAL

/* One row per mood, every field spelled out (the mockup's BASE with the
 * mood's overrides already applied):
 *   colour, blush, eyes L R, mouth, blinks, fx,
 *   esx esy spr gox goy,  ltL ltR lrL lrR lbL lbR lhL lhR core,
 *   byL byR brL brR blush os,  gaze bx by big min max */
#define ROW(c, bl, el, er, mo, bk, fx, esx, esy, spr, gox, goy, ltL, ltR, lrL, lrR, lbL, lbR, lhL, lhR, core, \
            byL, byR, brL, brR, blush, os, gbx, gby, gbig, gmin, gmax)                                           \
    { c, bl, el, er, mo, bk, fx, esx, esy, spr, gox, goy, ltL, ltR, lrL, lrR, lbL, lbR, lhL, lhR, core,           \
      byL, byR, brL, brR, blush, os, gbx, gby, gbig, gmin, gmax }

#define BL JOLLY_FACE_BLUSH

static const jolly_face_mood_t FACES[JM_COUNT] = {
    [JM_TRANQUILO] = ROW(0x8BEBFF, BL, O, O, JFM_SMILE, true, 0,
                         1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -4, 4, 0, 1, 40, 16, .55f, 650, 2600),
    [JM_ESCUCHANDO] = ROW(0x5CE1FF, BL, O, O, JFM_RING, true, 0,
                          1.05f, 1.1f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -12, -12, -6, 6, 0, .5f, 6, 4, .25f, 900, 2200),
    [JM_PENSANDO] = ROW(0x9FDBFF, BL, O, O, JFM_SIDE, true, 0,
                        .92f, .92f, 0, 26, -22, 0, 0, 0, 0, .24f, .24f, 0, 0, 0, 3, -16, 5, -12, 0, 1, 8, 5, .3f, 1200, 2800),
    [JM_HABLANDO] = ROW(0x6FE6FF, BL, O, O, JFM_PILL, true, 0,
                        1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -4, -4, -4, 4, 0, 1, 10, 6, .3f, 700, 1800),
    [JM_FELIZ] = ROW(0x8CF5E0, BL, O, O, JFM_PILL, true, JFX_TONGUE,
                     1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, .42f, .42f, 0, -12, -12, -8, 8, .5f, 1, 14, 6, .4f, 800, 2000),
    [JM_CARINO] = ROW(0xFF9ECF, BL, U, U, JFM_BIG, false, JFX_HEARTS,
                      1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -10, -10, -12, 12, 1, 1, 0, 0, 0, 0, 0),
    [JM_DURMIENDO] = ROW(0x6F86C9, BL, D, D, JFM_RING, false, JFX_ZZZ,
                         1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 8, 0, 0, 0, .32f, 0, 0, 0, 0, 0),
    [JM_ESPIA] = ROW(0xFFC145, BL, O, O, JFM_SMIRK, true, 0,
                     1, 1, 0, -26, 0, .44f, .52f, 0, 0, .16f, .16f, 0, 0, 0, -16, 6, -14, -4, 0, 1, 10, 4, .5f, 900, 1800),
    [JM_ALERTA] = ROW(0xFF5C6C, BL, O, O, JFM_RING, true, JFX_ALARM | JFX_SHAKE,
                      .96f, 1.18f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -24, -24, -6, 6, 0, 1, 30, 12, .85f, 220, 600),
    [JM_DESCONECTADO] = ROW(0x5D6380, BL, O, O, JFM_FLAT, false, 0,
                            1, .1f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 10, 0, 0, 0, 1, 0, 0, 0, 0, 0),
    [JM_MAREADO] = ROW(0xB9A6FF, BL, S, S, JFM_ZIG, false, JFX_WOBBLE,
                       1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -8, -2, -12, 16, 0, 1, 0, 0, 0, 0, 0),
    [JM_ASUSTADO] = ROW(0xD7DCFF, BL, O, O, JFM_WAVE, false, JFX_DROP | JFX_TREMBLE | JFX_ROUND_EYES,
                        .6f, .66f, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -26, -26, -12, 12, 0, 1, 24, 10, .9f, 140, 380),
    [JM_ALIVIADO] = ROW(0x9FF5D0, BL, O, O, JFM_SMILE, true, JFX_PUFF,
                        1, 1, 0, 0, 0, .4f, .4f, 0, 0, 0, 0, 0, 0, 0, 4, 4, 8, -8, .3f, 1, 12, 4, .3f, 1400, 2800),
    [JM_ENOJADO] = ROW(0xFF7A45, 0xFF5C6C, O, O, JFM_GRILL, true, JFX_RAGE,
                       1, 1, 0, 0, 0, .28f, .28f, 16, -16, 0, 0, 0, 0, 0, 12, 12, 22, -22, .7f, 1, 8, 4, .3f, 900, 2000),
    [JM_SORPRENDIDO] = ROW(0xFFE066, BL, O, O, JFM_RING, true, 0,
                           1.12f, 1.28f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -30, -30, -4, 4, 0, 1.25f, 6, 4, .2f, 1000, 2400),
    [JM_GUINO] = ROW(0x7FF0B0, BL, O, U, JFM_SMILE, false, JFX_WAG_TONGUE,
                     1, 1, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -14, 5, -9, 8, 0, 1, 10, 5, .3f, 900, 2000),
};

const jolly_face_mood_t *jolly_face_mood(jolly_mood_t m)
{
    return &FACES[(unsigned)m < JM_COUNT ? m : JM_TRANQUILO];
}

static const char *const NAMES[JM_COUNT] = {
    "tranquilo", "escuchando", "pensando", "hablando", "feliz", "carino", "durmiendo", "espia",
    "alerta", "desconectado", "mareado", "asustado", "aliviado", "enojado", "sorprendido", "guino",
};

const char *jolly_face_name(jolly_mood_t m)
{
    return NAMES[(unsigned)m < JM_COUNT ? m : JM_TRANQUILO];
}

jolly_mood_t jolly_face_named(const char *name)
{
    for (int i = 0; i < JM_COUNT; i++) {
        if (!strcmp(name, NAMES[i])) {
            return (jolly_mood_t)i;
        }
    }
    return JM_COUNT;
}

jolly_viseme_t jolly_face_viseme(float level, bool round)
{
    if (level < .08f) {
        return (jolly_viseme_t){ JV_CERRADA, .8f, .16f };
    }
    if (round && level > .45f) {
        return (jolly_viseme_t){ JV_O, .55f, .95f };
    }
    if (level < .32f) {
        return (jolly_viseme_t){ JV_CHICA, .6f, .42f };
    }
    if (level < .6f) {
        return (jolly_viseme_t){ JV_MEDIA, .8f, .66f };
    }
    return (jolly_viseme_t){ JV_ANCHA, 1.05f, .86f };
}

void jolly_spring_step(float *x, float *v, float target, float k, float damp)
{
    *v = (*v + (target - *x) * k) * damp;
    *x += *v;
}

float jolly_face_blink(float p, bool slow)
{
    float hold = slow ? .65f : .5f;
    if (p <= 0) {
        return 0;
    }
    if (p < .45f) {
        return p / .45f;
    }
    if (p < hold) {
        return 1;
    }
    float b = 1 - (p - hold) / (1 - hold);
    return b > 0 ? b : 0;
}

float jolly_face_turn(float gx, int side)
{
    float d = gx * .004f;
    d = d > .16f ? .16f : d < -.16f ? -.16f : d;
    return side ? 1 - d : 1 + d;
}

/* The mockup's SVG paths, viewBox 0 0 120 50 centred on the mouth. */
#define CX 60
#define CY 25

static int put(int16_t *xy, int n, int max, float x, float y)
{
    if (n < max) {
        xy[2 * n] = (int16_t)(x < 0 ? x - .5f : x + .5f) - CX;
        xy[2 * n + 1] = (int16_t)(y < 0 ? y - .5f : y + .5f) - CY;
    }
    return n + 1;
}

/* A quadratic Bézier from p0 over c to p1, `steps` segments, without p0. */
static int quad(int16_t *xy, int n, int max, float x0, float y0, float cx, float cy, float x1, float y1, int steps)
{
    for (int i = 1; i <= steps; i++) {
        float t = (float)i / steps, u = 1 - t;
        n = put(xy, n, max, u * u * x0 + 2 * u * t * cx + t * t * x1, u * u * y0 + 2 * u * t * cy + t * t * y1);
    }
    return n;
}

int jolly_face_mouth_line(jolly_face_mouth_t kind, int16_t *xy, int max)
{
    int n = 0;
    switch (kind) {
    case JFM_SMILE:   /* M24 16 Q60 44 96 16 */
        n = put(xy, n, max, 24, 16);
        n = quad(xy, n, max, 24, 16, 60, 44, 96, 16, 8);
        break;
    case JFM_BIG:     /* M14 10 Q60 54 106 10 */
        n = put(xy, n, max, 14, 10);
        n = quad(xy, n, max, 14, 10, 60, 54, 106, 10, 8);
        break;
    case JFM_SMIRK:   /* M32 28 Q66 34 98 12 */
        n = put(xy, n, max, 32, 28);
        n = quad(xy, n, max, 32, 28, 66, 34, 98, 12, 8);
        break;
    case JFM_SIDE:    /* M66 30 L100 22 */
        n = put(xy, n, max, 66, 30);
        n = put(xy, n, max, 100, 22);
        break;
    case JFM_FLAT:    /* M34 26 L86 26 */
        n = put(xy, n, max, 34, 26);
        n = put(xy, n, max, 86, 26);
        break;
    case JFM_ZIG: {   /* M12 26 L24 16 L36 36 ... L96 16 L108 26 */
        static const uint8_t Z[9][2] = { { 12, 26 }, { 24, 16 }, { 36, 36 }, { 48, 16 }, { 60, 36 },
                                         { 72, 16 }, { 84, 36 }, { 96, 16 }, { 108, 26 } };
        for (int i = 0; i < 9; i++) {
            n = put(xy, n, max, Z[i][0], Z[i][1]);
        }
        break;
    }
    case JFM_WAVE: {  /* M30 26 q7.5 -8 15 0 t15 0 t15 0 t15 0 */
        n = put(xy, n, max, 30, 26);
        for (int s = 0; s < 4; s++) {
            float x0 = 30 + 15 * s;
            n = quad(xy, n, max, x0, 26, x0 + 7.5f, s % 2 ? 34 : 18, x0 + 15, 26, 3);
        }
        break;
    }
    default:
        return 0;
    }
    return n < max ? n : max;
}
