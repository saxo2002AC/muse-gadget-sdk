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
 * Jolly's mouth, under the eyes and in their colour (robot style): a smile
 * arc, a flat line, an "o" or a talking mouth that opens and closes. Pure,
 * so tests/jolly/test_mouth.c checks the mapping; jolly_eyes.c draws it.
 */

typedef enum {
    JMOUTH_SMILE,   /* an arc, w px across */
    JMOUTH_LINE,    /* a rounded bar, w x thick */
    JMOUTH_O,       /* a ring, w px across */
    JMOUTH_TALK,    /* a rounded rect w across, its height jolly_mouth_talk_h() */
} jolly_mouth_kind_t;

typedef struct {
    jolly_mouth_kind_t kind;
    int16_t w;      /* px */
    int16_t thick;  /* stroke, px */
    int16_t dx;     /* off centre, px: pensando's mouth goes to one side */
    bool jitter;    /* trembles with the eyes (alerta) */
    bool bubble;    /* a snore bubble beside it (durmiendo) */
} jolly_mouth_t;

#define JOLLY_MOUTH_TALK_MIN 6    /* closed, px */
#define JOLLY_MOUTH_TALK_MAX 34   /* wide open */

jolly_mouth_t jolly_mouth_for(jolly_mood_t mood);

/* The talking mouth's opening: from the reply's level (0..1, muse_state's)
 * when it's above a whisper, else a natural open-and-close from the clock. */
int jolly_mouth_talk_h(float level, int64_t now_ms);

#ifdef __cplusplus
}
#endif
