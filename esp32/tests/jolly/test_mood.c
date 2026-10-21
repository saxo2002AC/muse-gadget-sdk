/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * jolly_mood_pick: the guardian plan's mood arbitration (§1.9), first match wins.
 */
#include <assert.h>

#include "jolly/jolly_mood.h"

static jolly_mood_in_t base(void)
{
    jolly_mood_in_t in = {
        .voice = MUSE_MODE_IDLE, .link = JL_OK, .level = JLV_OK, .sec_level = JLV_OK,
        .quiet = false, .now_ms = 100000, .pet_until_ms = 0, .feliz_until_ms = 0,
    };
    return in;
}

static void each_rule(void)
{
    jolly_mood_in_t in = base();
    assert(jolly_mood_pick(&in) == JM_TRANQUILO);
    in.voice = MUSE_MODE_LISTENING;
    assert(jolly_mood_pick(&in) == JM_ESCUCHANDO);
    in.voice = MUSE_MODE_THINKING;
    assert(jolly_mood_pick(&in) == JM_PENSANDO);
    in.voice = MUSE_MODE_SPEAKING;
    assert(jolly_mood_pick(&in) == JM_HABLANDO);
    in = base();
    in.pet_until_ms = in.now_ms + 1;
    assert(jolly_mood_pick(&in) == JM_CARINO);
    in = base();
    for (jolly_link_t l = JL_WIFI_DOWN; l <= JL_STALE; l++) {
        in.link = l;
        assert(jolly_mood_pick(&in) == JM_DESCONECTADO);
    }
    in = base();
    in.level = JLV_CRIT;
    assert(jolly_mood_pick(&in) == JM_ALERTA);
    in = base();
    in.sec_level = JLV_WARN;
    in.level = JLV_WARN;
    assert(jolly_mood_pick(&in) == JM_ESPIA);
    in = base();
    in.feliz_until_ms = in.now_ms + 1;
    assert(jolly_mood_pick(&in) == JM_FELIZ);
    in = base();
    in.quiet = true;
    assert(jolly_mood_pick(&in) == JM_DURMIENDO);
}

static void precedence(void)
{
    jolly_mood_in_t in = base();
    in.voice = MUSE_MODE_SPEAKING;           /* voice_beats_pet */
    in.pet_until_ms = in.now_ms + 1000;
    assert(jolly_mood_pick(&in) == JM_HABLANDO);
    in = base();                             /* pet_beats_disconnected */
    in.pet_until_ms = in.now_ms + 1000;
    in.link = JL_UNREACHABLE;
    assert(jolly_mood_pick(&in) == JM_CARINO);
    in = base();                             /* disconnected_beats_crit */
    in.link = JL_STALE;
    in.level = JLV_CRIT;
    assert(jolly_mood_pick(&in) == JM_DESCONECTADO);
    in = base();                             /* crit_beats_spy */
    in.level = JLV_CRIT;
    in.sec_level = JLV_WARN;
    assert(jolly_mood_pick(&in) == JM_ALERTA);
    in = base();                             /* spy beats feliz and quiet */
    in.sec_level = JLV_WARN;
    in.feliz_until_ms = in.now_ms + 1000;
    in.quiet = true;
    assert(jolly_mood_pick(&in) == JM_ESPIA);
    in = base();                             /* feliz beats quiet */
    in.feliz_until_ms = in.now_ms + 1000;
    in.quiet = true;
    assert(jolly_mood_pick(&in) == JM_FELIZ);
    in = base();                             /* the error and boot modes aren't voice */
    in.voice = MUSE_MODE_ERROR;
    assert(jolly_mood_pick(&in) == JM_TRANQUILO);
}

static void timers(void)
{
    jolly_mood_in_t in = base();             /* feliz_expires and the pet's end */
    in.feliz_until_ms = in.now_ms;
    assert(jolly_mood_pick(&in) == JM_TRANQUILO);
    in.pet_until_ms = in.now_ms;
    assert(jolly_mood_pick(&in) == JM_TRANQUILO);
    in = base();                             /* quiet_only_when_ok */
    in.quiet = true;
    in.level = JLV_WARN;
    assert(jolly_mood_pick(&in) == JM_TRANQUILO);   /* ops_warn_stays_tranquilo */
    in.quiet = false;
    assert(jolly_mood_pick(&in) == JM_TRANQUILO);

    /* Feliz for 4 s on a crit -> ok transition, and only then. */
    assert(jolly_mood_feliz_until(JLV_CRIT, JLV_OK, 5000, 0) == 5000 + JOLLY_FELIZ_MS);
    assert(jolly_mood_feliz_until(JLV_WARN, JLV_OK, 5000, 0) == 0);
    assert(jolly_mood_feliz_until(JLV_CRIT, JLV_WARN, 5000, 0) == 0);
    assert(jolly_mood_feliz_until(JLV_OK, JLV_OK, 5000, 7000) == 7000);
    assert(jolly_mood_pet_until(5000) == 5000 + JOLLY_PET_MS);
}

int main(void)
{
    each_rule();
    precedence();
    timers();
    return 0;
}
