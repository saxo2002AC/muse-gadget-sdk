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

#include "jolly/jolly_alarm.h"

#include <string.h>

void jolly_alarm_init(jolly_alarm_t *a)
{
    memset(a, 0, sizeof(*a));
}

static bool has_crit(const jolly_alarm_obs_t *o)
{
    return o->level == JLV_CRIT || o->sec == JLV_CRIT || o->crit > 0;
}

void jolly_alarm_feed(jolly_alarm_t *a, const jolly_alarm_obs_t *o, int64_t now, int32_t alarm_s)
{
    if (!jolly_alarm_s_ok(alarm_s)) {
        alarm_s = JOLLY_ALARM_DEFAULT_S;
    }
    int32_t secs = 0;
    if (!a->seeded) {
        /* At boot nothing is new, but an open crit gets a short alarm. */
        a->seeded = true;
        if (has_crit(o)) {
            secs = o->sec == JLV_CRIT ? JOLLY_ALARM_SEC_S : alarm_s;
            secs = secs < JOLLY_ALARM_BOOT_S ? secs : JOLLY_ALARM_BOOT_S;
            a->until = now + secs * 1000LL;
        }
    } else if (!has_crit(o)) {
        a->until = 0;   /* all clear: no alarm, no reminders */
    } else {
        bool sec_new = o->sec == JLV_CRIT && a->last.sec != JLV_CRIT;
        bool now_new = sec_new || o->crit > a->last.crit || (o->level == JLV_CRIT && a->last.level != JLV_CRIT) ||
                       !has_crit(&a->last);
        if (now_new) {
            secs = sec_new ? JOLLY_ALARM_SEC_S : alarm_s;
            int64_t until = now + secs * 1000LL;
            /* A new ops crit doesn't cut a security alarm short. */
            a->until = until > a->until || !jolly_alarm_active(a, now) ? until : a->until;
        }
    }
    a->last = *o;
}

bool jolly_alarm_active(const jolly_alarm_t *a, int64_t now)
{
    return has_crit(&a->last) && a->until && now < a->until;
}

jolly_mood_t jolly_alarm_mood(const jolly_alarm_t *a, int64_t now)
{
    if (!a->seeded || !has_crit(&a->last) || !a->until) {
        return JM_TRANQUILO;
    }
    if (now < a->until) {
        return JM_ALERTA;
    }
    int64_t since = now - a->until;
    if (since >= JOLLY_REMIND_EVERY_MS && since % JOLLY_REMIND_EVERY_MS < JOLLY_REMIND_MS) {
        return a->last.sec == JLV_CRIT ? JM_ESPIA : JM_SORPRENDIDO;
    }
    return JM_TRANQUILO;
}

bool jolly_alarm_s_ok(int32_t s)
{
    return s >= 0 && s <= JOLLY_ALARM_MAX_S;
}
