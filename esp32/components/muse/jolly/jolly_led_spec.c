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

#include "jolly/jolly_led.h"

#include <math.h>

static jolly_rgb_t rgb(int r, int g, int b)
{
    return (jolly_rgb_t){ (uint8_t)r, (uint8_t)g, (uint8_t)b };
}

/* Full-scale colour times k (0..1). */
static jolly_rgb_t mul(jolly_rgb_t c, float k)
{
    k = k < 0 ? 0 : k > 1 ? 1 : k;
    return rgb((int)(c.r * k + .5f), (int)(c.g * k + .5f), (int)(c.b * k + .5f));
}

static jolly_rgb_t pick(const jolly_led_in_t *in)
{
    static const jolly_rgb_t BLUE = { 0, 60, 255 }, PURPLE = { 140, 0, 255 }, CYAN = { 0, 200, 255 },
                             PINK = { 255, 60, 160 }, RED = { 255, 0, 0 }, AMBER = { 255, 120, 0 },
                             GREY = { 40, 40, 40 }, GREEN = { 0, 50, 0 }, OFF = { 0, 0, 0 };
    int64_t t = in->now_ms < 0 ? 0 : in->now_ms;
    if (in->asleep && !in->alarming) {
        return OFF;
    }
    switch (in->mood) {
    case JM_ESCUCHANDO:
        return BLUE;
    case JM_PENSANDO: {
        float p = (float)(t % JOLLY_LED_BREATH_MS) / JOLLY_LED_BREATH_MS;
        float w = .5f - .5f * cosf(2 * 3.14159265f * p);   /* 0 → 1 → 0 */
        jolly_rgb_t c = rgb((int)(BLUE.r + (PURPLE.r - BLUE.r) * w), (int)(BLUE.g + (PURPLE.g - BLUE.g) * w),
                            (int)(BLUE.b + (PURPLE.b - BLUE.b) * w));
        return mul(c, .35f + .65f * w);
    }
    case JM_HABLANDO: {
        float v = in->voice_level != in->voice_level ? 0 : in->voice_level;   /* NaN */
        return mul(CYAN, .2f + .8f * v);
    }
    case JM_CARINO:
        return PINK;
    default:
        break;
    }
    if (in->alarming) {
        return (t / JOLLY_LED_ALARM_MS) % 2 ? OFF : RED;
    }
    if (in->link != JL_OK || in->mood == JM_DESCONECTADO) {
        return GREY;
    }
    if (in->mood == JM_DURMIENDO) {
        return OFF;
    }
    if (in->level == JLV_CRIT) {
        return mul(RED, .25f);
    }
    if (in->level == JLV_WARN) {
        return t % JOLLY_LED_WARN_MS < 600 ? AMBER : OFF;
    }
    return in->beat && t % JOLLY_LED_BEAT_EVERY_MS < JOLLY_LED_BEAT_ON_MS ? GREEN : OFF;
}

jolly_rgb_t jolly_led_color(const jolly_led_in_t *in)
{
    int b = in->brightness < 0 ? 0 : in->brightness > 100 ? 100 : in->brightness;
    if (!b) {
        return rgb(0, 0, 0);
    }
    jolly_rgb_t c = pick(in);
    return rgb(c.r * b / 100, c.g * b / 100, c.b * b / 100);
}
