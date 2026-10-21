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

#include "muse_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Jolly's mood: what the eyes show, from Muse's voice, a recent pat, the
 * link to the Guardián and the Mac Mini's health (the guardian plan, §1.9).
 * Pure, so tests/jolly/test_mood.c checks it on the host.
 */

typedef enum {
    JM_TRANQUILO, JM_ESCUCHANDO, JM_PENSANDO, JM_HABLANDO, JM_FELIZ, JM_CARINO,
    JM_DURMIENDO, JM_ESPIA, JM_ALERTA, JM_DESCONECTADO,
    JM_MAREADO, JM_ASUSTADO, JM_ALIVIADO, JM_ENOJADO,   /* from the IMU (jolly_motion.h) */
    JM_SORPRENDIDO, JM_GUINO,                           /* face v5: news arriving, good news */
    JM_COUNT,
} jolly_mood_t;

/* The board's link to the Guardián (§1.8). */
typedef enum { JL_OK, JL_WIFI_DOWN, JL_UNREACHABLE, JL_UNAUTH, JL_STALE } jolly_link_t;

typedef enum { JLV_OK, JLV_WARN, JLV_CRIT } jolly_level_t;

#define JOLLY_PET_MS 2600     /* how long a pat keeps Jolly in cariño */
#define JOLLY_FELIZ_MS 4000   /* how long Jolly is happy once a crit clears */
#define JOLLY_SORPRENDIDO_MS 2000   /* startled when the level worsens */
#define JOLLY_GUINO_MS 2400         /* a wink when a warning clears: good news */

typedef struct {
    muse_mode_t voice;        /* Muse's mode: listening, thinking or speaking win */
    jolly_link_t link;
    jolly_level_t level;      /* the Mini's, from the glance */
    jolly_level_t sec_level;  /* its security categories only */
    bool quiet;               /* the Guardián's quiet hours */
    int64_t now_ms;           /* the board's monotonic clock */
    int64_t pet_until_ms;     /* cariño until then (jolly_mood_pet_until) */
    int64_t feliz_until_ms;   /* feliz until then (jolly_mood_feliz_until) */
    jolly_mood_t motion;      /* what the IMU asks for (jolly_motion.h); JM_TRANQUILO for nothing */
    int64_t surprise_until_ms;   /* sorprendido until then (jolly_mood_surprise_until) */
    int64_t wink_until_ms;       /* guiño until then (jolly_mood_wink_until) */
} jolly_mood_in_t;

/* The mood, first match wins: voice, pat, a motion mood (unless a current
 * crit: security still wins over play), no link, crit, the level just
 * worsened (sorprendido), a security warning, a warning just cleared (guiño),
 * a crit just cleared (feliz), quiet hours, else tranquilo. An ops warning alone stays
 * tranquilo (its count shows on the screen instead). */
jolly_mood_t jolly_mood_pick(const jolly_mood_in_t *in);

/* feliz_until after the level moved from `prev` to `cur` at `now_ms`: a crit
 * clearing to ok starts JOLLY_FELIZ_MS of it; anything else keeps the old. */
int64_t jolly_mood_feliz_until(jolly_level_t prev, jolly_level_t cur, int64_t now_ms, int64_t feliz_until_ms);

/* surprise_until after the level moved from `prev` to `cur`: any worsening
 * starts JOLLY_SORPRENDIDO_MS of it; anything else keeps the old. */
int64_t jolly_mood_surprise_until(jolly_level_t prev, jolly_level_t cur, int64_t now_ms, int64_t surprise_until_ms);

/* wink_until after the level moved from `prev` to `cur`: a warning clearing
 * to ok starts JOLLY_GUINO_MS of it (a crit clearing is feliz instead). */
int64_t jolly_mood_wink_until(jolly_level_t prev, jolly_level_t cur, int64_t now_ms, int64_t wink_until_ms);

/* pet_until for a pat at `now_ms`. */
int64_t jolly_mood_pet_until(int64_t now_ms);

#ifdef __cplusplus
}
#endif
