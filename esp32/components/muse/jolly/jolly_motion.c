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

#include "jolly/jolly_motion.h"

#include <math.h>
#include <string.h>

#define SHAKE_STD_MG 450
#define SHAKE_MS 400
#define SHAKE_REARM_MS 1000        /* calm this long before another shake counts */
#define FF_MG 300
#define FF_MS 120
#define LAND_MG 2000
#define LAND_WINDOW_MS 1500
#define KNOCK_MG 2500
#define KNOCK_SAMPLES 3            /* 30 ms at 100 Hz */
#define AFTER_LAND_MS 200          /* a landing's bounces aren't knocks */
#define DOWN_Z_MG (-800)
#define DOWN_XY_MG 400
#define DOWN_MS 2000
#define UP_MS 500

void jolly_motion_init(jolly_motion_t *m)
{
    memset(m, 0, sizeof(*m));
    m->landed_at = INT64_MIN / 2;
}

static int16_t magnitude(int16_t ax, int16_t ay, int16_t az)
{
    float v = sqrtf((float)ax * ax + (float)ay * ay + (float)az * az);
    return (int16_t)(v > 32767.0f ? 32767.0f : v);
}

/* The acceleration vector's spread over the window: sqrt(var x + var y + var z).
 * Not |a|'s: shaking along one axis can keep |a| nearly constant. */
static float spread(const jolly_motion_t *m)
{
    if (m->n < JOLLY_MOTION_WINDOW / 2) {
        return 0.0f;
    }
    float total = 0;
    for (int k = 0; k < 3; k++) {
        float sum = 0, sq = 0;
        for (int i = 0; i < m->n; i++) {
            sum += m->axis[k][i];
            sq += (float)m->axis[k][i] * m->axis[k][i];
        }
        float mean = sum / m->n, var = sq / m->n - mean * mean;
        total += var > 0 ? var : 0;
    }
    return sqrtf(total);
}

jolly_motion_ev_t jolly_motion_feed(jolly_motion_t *m, int16_t ax, int16_t ay, int16_t az, int64_t now, bool gate)
{
    int16_t mag = magnitude(ax, ay, az);
    m->axis[0][m->at] = ax;
    m->axis[1][m->at] = ay;
    m->axis[2][m->at] = az;
    m->at = (m->at + 1) % JOLLY_MOTION_WINDOW;
    m->n = m->n < JOLLY_MOTION_WINDOW ? m->n + 1 : m->n;
    jolly_motion_ev_t ev = JME_NONE;

    /* Freefall, then its landing. */
    if (mag < FF_MG) {
        if (!m->ff_since) {
            m->ff_since = now;
        }
        if (!m->ff_fired && now - m->ff_since > FF_MS) {
            m->ff_fired = true;
            m->land_until = now + LAND_WINDOW_MS;
            ev = JME_FREEFALL;
        }
    } else {
        m->ff_since = 0;
        m->ff_fired = false;
        if (m->land_until && now <= m->land_until && mag > LAND_MG) {
            m->land_until = 0;
            m->landed_at = now;
            m->spike = 0;
            return JME_LAND;
        }
    }
    if (m->land_until && now > m->land_until) {
        m->land_until = 0;   /* no landing seen: just over */
    }

    /* Shaking: a wide spread of |a|, held. */
    bool shaking = spread(m) > SHAKE_STD_MG;
    if (shaking) {
        m->calm_since = 0;
        if (!m->shake_since) {
            m->shake_since = now;
        }
        if (!m->shake_fired && now - m->shake_since >= SHAKE_MS && ev == JME_NONE) {
            m->shake_fired = true;
            ev = JME_SHAKE;
        }
    } else {
        m->shake_since = 0;
        if (!m->calm_since) {
            m->calm_since = now;
        }
        if (now - m->calm_since >= SHAKE_REARM_MS) {
            m->shake_fired = false;
        }
    }

    /* A knock: a short sharp spike, ended. */
    if (mag > KNOCK_MG) {
        m->spike++;
    } else if (m->spike) {
        bool short_one = m->spike <= KNOCK_SAMPLES;
        bool landing = m->land_until || now - m->landed_at < AFTER_LAND_MS;
        m->spike = 0;
        if (short_one && gate && !landing && !shaking && !m->shake_since && ev == JME_NONE) {
            ev = JME_KNOCK;
        }
    }

    /* Face-down: gravity on -Z, held; face-up once it's gone for a moment. */
    bool down = az < DOWN_Z_MG && ax > -DOWN_XY_MG && ax < DOWN_XY_MG && ay > -DOWN_XY_MG && ay < DOWN_XY_MG;
    if (down) {
        m->up_since = 0;
        if (!m->down_since) {
            m->down_since = now;
        }
        if (!m->face_down && now - m->down_since >= DOWN_MS && ev == JME_NONE) {
            m->face_down = true;
            ev = JME_FACE_DOWN;
        }
    } else {
        m->down_since = 0;
        if (m->face_down) {
            if (!m->up_since) {
                m->up_since = now;
            }
            if (now - m->up_since >= UP_MS && ev == JME_NONE) {
                m->face_down = false;
                ev = JME_FACE_UP;
            }
        }
    }
    return ev;
}

void jolly_motion_mood_init(jolly_motion_mood_t *s)
{
    s->mood = JM_TRANQUILO;
    s->until = 0;
    s->face_down = false;
}

void jolly_motion_mood_on(jolly_motion_mood_t *s, jolly_motion_ev_t ev, int64_t now)
{
    switch (ev) {
    case JME_SHAKE:
        s->mood = JM_MAREADO;
        s->until = now + JOLLY_MAREADO_MS;
        break;
    case JME_FREEFALL:
        s->mood = JM_ASUSTADO;
        s->until = now + JOLLY_ASUSTADO_MS;
        break;
    case JME_LAND:
        s->mood = JM_ALIVIADO;
        s->until = now + JOLLY_ALIVIADO_MS;
        break;
    case JME_KNOCK:
        s->mood = JM_ENOJADO;
        s->until = now + JOLLY_ENOJADO_MS;
        break;
    case JME_FACE_DOWN:
        s->face_down = true;
        break;
    case JME_FACE_UP:
        s->face_down = false;
        break;
    default:
        break;
    }
}

jolly_mood_t jolly_motion_mood_now(const jolly_motion_mood_t *s, int64_t now)
{
    if (s->mood != JM_TRANQUILO && now < s->until) {
        return s->mood;
    }
    return s->face_down ? JM_DURMIENDO : JM_TRANQUILO;
}
