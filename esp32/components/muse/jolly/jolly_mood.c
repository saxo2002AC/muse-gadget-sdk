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

#include "jolly/jolly_mood.h"

jolly_mood_t jolly_mood_pick(const jolly_mood_in_t *in)
{
    switch (in->voice) {
    case MUSE_MODE_LISTENING:
        return JM_ESCUCHANDO;
    case MUSE_MODE_THINKING:
        return JM_PENSANDO;
    case MUSE_MODE_SPEAKING:
        return JM_HABLANDO;
    default:
        break;
    }
    if (in->now_ms < in->pet_until_ms) {
        return JM_CARINO;
    }
    bool alarming = in->link == JL_OK && in->alarm == JM_ALERTA;
    if (in->motion != JM_TRANQUILO && in->motion < JM_COUNT && !alarming) {
        return in->motion;
    }
    if (in->link != JL_OK) {
        return JM_DESCONECTADO;
    }
    if (alarming) {
        return JM_ALERTA;
    }
    if (in->now_ms < in->surprise_until_ms) {
        return JM_SORPRENDIDO;
    }
    if (in->alarm == JM_ESPIA || in->alarm == JM_SORPRENDIDO) {
        return in->alarm;   /* a standing crit's hourly glance */
    }
    if (in->sec_level == JLV_WARN) {
        return JM_ESPIA;
    }
    if (in->now_ms < in->wink_until_ms) {
        return JM_GUINO;
    }
    if (in->now_ms < in->feliz_until_ms) {
        return JM_FELIZ;
    }
    if (in->quiet && in->level == JLV_OK) {
        return JM_DURMIENDO;
    }
    return JM_TRANQUILO;
}

int64_t jolly_mood_feliz_until(jolly_level_t prev, jolly_level_t cur, int64_t now_ms, int64_t feliz_until_ms)
{
    return prev == JLV_CRIT && cur == JLV_OK ? now_ms + JOLLY_FELIZ_MS : feliz_until_ms;
}

int64_t jolly_mood_surprise_until(jolly_level_t prev, jolly_level_t cur, int64_t now_ms, int64_t surprise_until_ms)
{
    return cur > prev ? now_ms + JOLLY_SORPRENDIDO_MS : surprise_until_ms;
}

int64_t jolly_mood_wink_until(jolly_level_t prev, jolly_level_t cur, int64_t now_ms, int64_t wink_until_ms)
{
    return prev == JLV_WARN && cur == JLV_OK ? now_ms + JOLLY_GUINO_MS : wink_until_ms;
}

int64_t jolly_mood_pet_until(int64_t now_ms)
{
    return now_ms + JOLLY_PET_MS;
}
