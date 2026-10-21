/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * jolly_face_spec: face v5 (warroom-guardian/docs/mockup-cara-v5-fusion.html),
 * its 16 moods, visemes, springs, blinks, head turn and mouth lines.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "jolly/jolly_face_spec.h"

#define NEAR(a, b) (fabsf((float)(a) - (float)(b)) < 0.001f)

static void table(void)
{
    /* Colours, straight from the mockup's MOODS. */
    static const struct { jolly_mood_t m; uint32_t c; } C[] = {
        { JM_TRANQUILO, 0x8BEBFF }, { JM_ESCUCHANDO, 0x5CE1FF }, { JM_PENSANDO, 0x9FDBFF }, { JM_HABLANDO, 0x6FE6FF },
        { JM_FELIZ, 0x8CF5E0 }, { JM_CARINO, 0xFF9ECF }, { JM_DURMIENDO, 0x6F86C9 }, { JM_ESPIA, 0xFFC145 },
        { JM_ALERTA, 0xFF5C6C }, { JM_DESCONECTADO, 0x5D6380 }, { JM_MAREADO, 0xB9A6FF }, { JM_ENOJADO, 0xFF7A45 },
        { JM_SORPRENDIDO, 0xFFE066 }, { JM_GUINO, 0x7FF0B0 }, { JM_ASUSTADO, 0xD7DCFF }, { JM_ALIVIADO, 0x9FF5D0 },
    };
    assert(sizeof(C) / sizeof(C[0]) == JM_COUNT);   /* all 16 */
    for (size_t i = 0; i < JM_COUNT; i++) {
        const jolly_face_mood_t *f = jolly_face_mood(C[i].m);
        if (f->color != C[i].c) {
            fprintf(stderr, "mood %d: colour %06x, want %06x\n", (int)C[i].m, (unsigned)f->color, (unsigned)C[i].c);
            assert(0);
        }
    }
    assert(jolly_face_mood(JM_COUNT) == jolly_face_mood(JM_TRANQUILO));

    /* Eyes, mouths and effects as the mockup's CSS draws them. */
    const jolly_face_mood_t *f = jolly_face_mood(JM_TRANQUILO);
    assert(f->eye_l == JFE_OPEN && f->eye_r == JFE_OPEN && f->mouth == JFM_SMILE && f->blinks && f->gaze_bx == 40);
    assert(NEAR(f->brL, -4) && NEAR(f->brR, 4) && NEAR(f->esx, 1) && NEAR(f->os, 1));
    f = jolly_face_mood(JM_ESCUCHANDO);
    assert(f->mouth == JFM_RING && NEAR(f->os, .5f) && NEAR(f->esy, 1.1f) && NEAR(f->byL, -12));
    f = jolly_face_mood(JM_PENSANDO);
    assert(f->mouth == JFM_SIDE && NEAR(f->gox, 26) && NEAR(f->goy, -22) && NEAR(f->lbL, .24f));
    f = jolly_face_mood(JM_HABLANDO);
    assert(f->mouth == JFM_PILL && f->blinks);
    f = jolly_face_mood(JM_FELIZ);
    assert(f->mouth == JFM_PILL && (f->fx & JFX_TONGUE) && NEAR(f->lhL, .42f) && NEAR(f->blush, .5f));
    f = jolly_face_mood(JM_CARINO);
    assert(f->eye_l == JFE_CLOSED_UP && f->eye_r == JFE_CLOSED_UP && f->mouth == JFM_BIG && (f->fx & JFX_HEARTS));
    assert(!f->blinks && f->gaze_bx == 0 && NEAR(f->blush, 1));
    f = jolly_face_mood(JM_DURMIENDO);
    assert(f->eye_l == JFE_CLOSED_DOWN && f->mouth == JFM_RING && (f->fx & JFX_ZZZ) && NEAR(f->os, .32f));
    f = jolly_face_mood(JM_ESPIA);
    assert(f->mouth == JFM_SMIRK && NEAR(f->ltL, .44f) && NEAR(f->ltR, .52f) && NEAR(f->gox, -26));
    f = jolly_face_mood(JM_ALERTA);
    assert(f->mouth == JFM_RING && NEAR(f->core, 1) && (f->fx & JFX_ALARM) && (f->fx & JFX_SHAKE));
    f = jolly_face_mood(JM_DESCONECTADO);
    assert(f->mouth == JFM_FLAT && NEAR(f->esy, .1f) && !f->blinks && f->gaze_bx == 0);
    f = jolly_face_mood(JM_MAREADO);
    assert(f->eye_l == JFE_SPIRAL && f->eye_r == JFE_SPIRAL && f->mouth == JFM_ZIG && (f->fx & JFX_WOBBLE));
    f = jolly_face_mood(JM_ENOJADO);
    assert(f->mouth == JFM_GRILL && NEAR(f->lrL, 16) && NEAR(f->lrR, -16) && NEAR(f->brL, 22) && (f->fx & JFX_RAGE));
    assert(f->blush_color == 0xFF5C6C);
    f = jolly_face_mood(JM_SORPRENDIDO);
    assert(f->mouth == JFM_RING && NEAR(f->os, 1.25f) && NEAR(f->esy, 1.28f));
    f = jolly_face_mood(JM_GUINO);
    assert(f->eye_l == JFE_OPEN && f->eye_r == JFE_CLOSED_UP && f->mouth == JFM_SMILE && (f->fx & JFX_WAG_TONGUE));
    assert(!f->blinks);
    f = jolly_face_mood(JM_ASUSTADO);
    assert(f->mouth == JFM_WAVE && NEAR(f->spr, 12) && (f->fx & JFX_DROP) && (f->fx & JFX_TREMBLE) && (f->fx & JFX_ROUND_EYES));
    f = jolly_face_mood(JM_ALIVIADO);
    assert(f->mouth == JFM_SMILE && (f->fx & JFX_PUFF) && NEAR(f->ltL, .4f) && f->blinks);
}

static void visemes(void)
{
    jolly_viseme_t v = jolly_face_viseme(0.0f, false);
    assert(v.kind == JV_CERRADA && NEAR(v.mx, .8f) && NEAR(v.my, .16f));
    assert(jolly_face_viseme(0.2f, false).kind == JV_CHICA);
    assert(jolly_face_viseme(0.5f, false).kind == JV_MEDIA);
    v = jolly_face_viseme(0.9f, false);
    assert(v.kind == JV_ANCHA && NEAR(v.mx, 1.05f) && NEAR(v.my, .86f));
    v = jolly_face_viseme(0.5f, true);   /* a round vowel past the middle */
    assert(v.kind == JV_O && NEAR(v.my, .95f));
    assert(jolly_face_viseme(0.3f, true).kind == JV_CHICA);   /* too quiet to round */
}

static void motion(void)
{
    /* Springs settle on the target, overshooting first (anticipation's rebound). */
    float x = 0, v = 0, peak = 0;
    for (int i = 0; i < 200; i++) {
        jolly_spring_step(&x, &v, 1.0f, JOLLY_SPRING_K, JOLLY_SPRING_DAMP);
        peak = x > peak ? x : peak;
    }
    assert(peak > 1.0f && fabsf(x - 1.0f) < 0.01f);
    /* Blinks: shut at 45 %, held, open again; a slow one holds longer. */
    assert(NEAR(jolly_face_blink(0, false), 0) && NEAR(jolly_face_blink(.45f, false), 1));
    assert(NEAR(jolly_face_blink(.5f, false), 1) && jolly_face_blink(.6f, false) < 1);
    assert(NEAR(jolly_face_blink(.6f, true), 1) && NEAR(jolly_face_blink(1, false), 0));
    /* Head turn: the near eye widens to 116 %, the far one narrows to 84 %. */
    assert(NEAR(jolly_face_turn(0, 0), 1) && NEAR(jolly_face_turn(25, 0), 1.1f) && NEAR(jolly_face_turn(25, 1), .9f));
    assert(NEAR(jolly_face_turn(100, 0), 1.16f) && NEAR(jolly_face_turn(100, 1), .84f));
}

static void mouth_lines(void)
{
    int16_t xy[2 * JOLLY_MOUTH_POINTS];
    int n = jolly_face_mouth_line(JFM_SMILE, xy, JOLLY_MOUTH_POINTS);
    assert(n >= 5 && xy[0] == -36 && xy[1] == -9 && xy[2 * n - 2] == 36 && xy[2 * n - 1] == -9);
    assert(xy[2 * (n / 2) + 1] > 0);              /* the middle sags: a smile */
    n = jolly_face_mouth_line(JFM_ZIG, xy, JOLLY_MOUTH_POINTS);
    assert(n == 9 && xy[0] == -48 && xy[16] == 48);
    n = jolly_face_mouth_line(JFM_FLAT, xy, JOLLY_MOUTH_POINTS);
    assert(n == 2 && xy[1] == xy[3]);
    n = jolly_face_mouth_line(JFM_WAVE, xy, JOLLY_MOUTH_POINTS);
    assert(n >= 9 && xy[0] == -30 && xy[2 * n - 2] == 30);
    assert(jolly_face_mouth_line(JFM_PILL, xy, JOLLY_MOUTH_POINTS) == 0);   /* not a line */
}

int main(void)
{
    table();
    visemes();
    motion();
    mouth_lines();
    return 0;
}
