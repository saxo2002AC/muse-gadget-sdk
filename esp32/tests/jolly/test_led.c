/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * jolly_led_color: the WS2812's colour for Jolly's state, soft and quiet.
 */
#include <assert.h>
#include <stdio.h>

#include "jolly/jolly_led.h"

static jolly_led_in_t base(void)
{
    jolly_led_in_t in = { .mood = JM_TRANQUILO, .link = JL_OK, .level = JLV_OK, .alarming = false,
                          .voice_level = 0, .now_ms = 100000, .brightness = 100, .asleep = false, .beat = true };
    return in;
}

static jolly_rgb_t at(jolly_led_in_t in, int64_t t)
{
    in.now_ms = t;
    return jolly_led_color(&in);
}

static bool off(jolly_rgb_t c) { return !c.r && !c.g && !c.b; }

static void moods(void)
{
    jolly_led_in_t in = base();
    jolly_rgb_t c;

    in.mood = JM_ESCUCHANDO;                                      /* steady blue */
    c = at(in, 1000);
    assert(c.b > 100 && c.r < c.b / 4 && c.g < c.b / 2);
    assert(at(in, 1000).b == at(in, 2700).b);

    in.mood = JM_PENSANDO;                                        /* slow blue/purple breathing */
    jolly_rgb_t a = at(in, 0), b = at(in, JOLLY_LED_BREATH_MS / 2);
    assert(a.b > 0 && b.b > 0 && (a.r != b.r || a.b != b.b));
    assert(a.r == at(in, JOLLY_LED_BREATH_MS).r && a.b == at(in, JOLLY_LED_BREATH_MS).b);   /* periodic */

    in.mood = JM_HABLANDO;                                        /* cyan, with the voice */
    in.voice_level = 0;
    jolly_rgb_t quiet = at(in, 0);
    in.voice_level = 1;
    jolly_rgb_t loud = at(in, 0);
    assert(loud.g > quiet.g && loud.b > quiet.b && loud.r < loud.g / 4 && quiet.g > 0);

    in = base();
    in.mood = JM_CARINO;                                          /* pink */
    c = at(in, 0);
    assert(c.r > c.g && c.b > c.g && c.r > 100);

    in = base();
    in.mood = JM_DURMIENDO;                                       /* asleep or quiet hours: off */
    assert(off(at(in, 0)) && off(at(in, 10000)));
}

static void link_and_level(void)
{
    jolly_led_in_t in = base();

    in.alarming = true;                                           /* a new crit: red blink */
    in.level = JLV_CRIT;
    in.mood = JM_ALERTA;
    jolly_rgb_t on = at(in, 0), dark = at(in, JOLLY_LED_ALARM_MS);
    assert(on.r > 100 && !on.g && !on.b && off(dark));

    in.alarming = false;                                          /* standing crit: dim red, steady */
    in.mood = JM_TRANQUILO;
    jolly_rgb_t s = at(in, 0);
    assert(s.r > 0 && s.r < on.r / 2 && !s.g && !s.b && s.r == at(in, 7777).r);

    in.level = JLV_WARN;                                          /* warning: amber, slow blink */
    jolly_rgb_t w = at(in, 0);
    assert(w.r > w.g && w.g > w.b && w.g > 0);
    bool dark_seen = false;
    for (int64_t t = 0; t < JOLLY_LED_WARN_MS; t += 100) {
        dark_seen |= off(at(in, t));
    }
    assert(dark_seen);

    in = base();
    in.link = JL_UNREACHABLE;                                     /* no link: dim grey */
    in.mood = JM_DESCONECTADO;
    jolly_rgb_t g = at(in, 0);
    assert(g.r && g.r == g.g && g.g == g.b && g.r < 60);

    /* All fine: off, but a dim green blip every 10 s (if wanted). */
    in = base();
    assert(!off(at(in, 0)) && at(in, 0).g > at(in, 0).r);
    assert(off(at(in, JOLLY_LED_BEAT_ON_MS)) && off(at(in, 5000)));
    assert(!off(at(in, JOLLY_LED_BEAT_EVERY_MS)));
    in.beat = false;
    assert(off(at(in, 0)));
}

static void brightness_and_sleep(void)
{
    jolly_led_in_t in = base();
    in.mood = JM_ESCUCHANDO;
    jolly_rgb_t full = at(in, 0);
    in.brightness = 25;
    jolly_rgb_t quarter = at(in, 0);
    assert(quarter.b > 0 && quarter.b <= full.b / 4 + 1);
    in.brightness = 0;                                            /* >led=0: off */
    assert(off(at(in, 0)));

    in = base();
    in.asleep = true;                                             /* the screen sleeps: so does the LED */
    in.mood = JM_ESCUCHANDO;
    assert(off(at(in, 0)));
    in.alarming = true;                                           /* ... unless a crit is alarming */
    in.level = JLV_CRIT;
    in.mood = JM_ALERTA;
    assert(!off(at(in, 0)));

    /* The voice moods come first, as on the face. */
    in = base();
    in.level = JLV_CRIT;
    in.alarming = true;
    in.mood = JM_ESCUCHANDO;
    jolly_rgb_t l = at(in, 0);
    assert(l.b > l.r);
}

int main(void)
{
    moods();
    link_and_level();
    brightness_and_sleep();
    return 0;
}
