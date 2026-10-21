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

#include "jolly/jolly_mouth.h"

#include <math.h>

#define WHISPER 0.04f   /* below this the level says nothing: use the pattern */

jolly_mouth_t jolly_mouth_for(jolly_mood_t mood)
{
    switch (mood) {
    case JM_FELIZ:
    case JM_CARINO:
        return (jolly_mouth_t){ .kind = JMOUTH_SMILE, .w = 88, .thick = 9 };
    case JM_HABLANDO:
        return (jolly_mouth_t){ .kind = JMOUTH_TALK, .w = 46, .thick = 0 };
    case JM_ESCUCHANDO:
        return (jolly_mouth_t){ .kind = JMOUTH_O, .w = 20, .thick = 5 };
    case JM_ALERTA:
        return (jolly_mouth_t){ .kind = JMOUTH_O, .w = 38, .thick = 7, .jitter = true };
    case JM_PENSANDO:
        return (jolly_mouth_t){ .kind = JMOUTH_LINE, .w = 30, .thick = 7, .dx = 20 };
    case JM_DURMIENDO:
        return (jolly_mouth_t){ .kind = JMOUTH_LINE, .w = 18, .thick = 5, .bubble = true };
    case JM_DESCONECTADO:
        return (jolly_mouth_t){ .kind = JMOUTH_LINE, .w = 64, .thick = 6 };
    case JM_ESPIA:
        return (jolly_mouth_t){ .kind = JMOUTH_LINE, .w = 40, .thick = 7 };
    case JM_TRANQUILO:
    default:
        return (jolly_mouth_t){ .kind = JMOUTH_SMILE, .w = 56, .thick = 7 };
    }
}

int jolly_mouth_talk_h(float level, int64_t now_ms)
{
    const int span = JOLLY_MOUTH_TALK_MAX - JOLLY_MOUTH_TALK_MIN;
    float open;
    if (level > WHISPER) {
        open = level > 1.0f ? 1.0f : level;
    } else {
        /* Syllables (about 3 a second) under a slower phrase envelope. */
        float t = (float)now_ms;
        float syllable = 0.5f + 0.5f * sinf(t * 0.0195f);
        float phrase = 0.55f + 0.45f * sinf(t * 0.0037f + 1.3f);
        open = syllable * phrase;
    }
    return JOLLY_MOUTH_TALK_MIN + (int)(open * span + 0.5f);
}
