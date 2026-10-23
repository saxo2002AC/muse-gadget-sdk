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
 * The ES3C40P's WS2812 (GPIO42) shows Jolly's state, softly. First match:
 *   >led=0, or the screen asleep (unless a crit is alarming)   off
 *   escuchando  steady blue          pensando  blue/purple breathing, 3 s
 *   hablando    cyan with the voice  cariño    pink
 *   a new crit, alarming             red blink, 1 s
 *   no link (desconectado)           dim grey
 *   durmiendo, quiet hours           off
 *   a standing crit                  dim red, steady
 *   a warning                        amber, a slow blink
 *   all fine                         off, or a dim green blip every 10 s (>led_beat=1)
 * Brightness >led=0..100 (25 by default). jolly_led_color() is pure:
 * tests/jolly/test_led.c.
 */

#define JOLLY_LED_BREATH_MS 3000
#define JOLLY_LED_ALARM_MS 500        /* on, then off as long */
#define JOLLY_LED_WARN_MS 2000        /* a 600 ms glow every 2 s */
#define JOLLY_LED_BEAT_EVERY_MS 10000
#define JOLLY_LED_BEAT_ON_MS 200
#define JOLLY_LED_DEFAULT_BRIGHTNESS 25

typedef struct {
    uint8_t r, g, b;
} jolly_rgb_t;

typedef struct {
    jolly_mood_t mood;             /* what the face shows */
    jolly_link_t link;
    jolly_level_t level;           /* the Mini's */
    bool alarming;                 /* a new crit, still alarming (jolly_alarm.h) */
    float voice_level;             /* 0..1, while speaking */
    int64_t now_ms;
    int brightness;                /* 0..100 */
    bool asleep;                   /* the screen sleeps */
    bool beat;                     /* the all-fine heartbeat */
} jolly_led_in_t;

jolly_rgb_t jolly_led_color(const jolly_led_in_t *in);

/* The device side (jolly_led.c): the RMT strip on GPIO42, NVS (jolly_led). */
void jolly_led_start(void);
void jolly_led_show(const jolly_led_in_t *state);   /* brightness and beat are filled in here */

/* >led=0..100, >led_beat=0|1, >led: @led {brightness, beat, rgb}. */
bool jolly_led_console_command(const char *line);

#ifdef __cplusplus
}
#endif
