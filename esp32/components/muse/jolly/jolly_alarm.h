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
 * When a crit makes Jolly alarmed (Andrés's rule): a NEW crit shows alerta
 * for a while, then the face calms down, though the chip stays red. A crit
 * that stands (a backup crit for days) never alarms again; it only reminds,
 * with a short glance once an hour (espía for a security crit, sorprendido
 * otherwise). A crit already open at boot alarms 30 s at most.
 *
 * New is: the crit count went up, the level reached crit, or the security
 * level reached crit (security crits alarm for 10 min). The glance has no
 * alert ids, and its headline's text can change for the same alert ("10
 * días", "11 días"), so neither is used. Pure: tests/jolly/test_alarm.c.
 */

#define JOLLY_ALARM_DEFAULT_S 180   /* >grd_alarm_s=, 0..3600 */
#define JOLLY_ALARM_MAX_S 3600
#define JOLLY_ALARM_SEC_S 600       /* a security crit's */
#define JOLLY_ALARM_BOOT_S 30       /* at most, for a crit already open at boot */
#define JOLLY_REMIND_EVERY_MS (60LL * 60 * 1000)
#define JOLLY_REMIND_MS 4000

/* What a glance says, as far as alarms go. */
typedef struct {
    jolly_level_t level;           /* the Mini's */
    jolly_level_t sec;             /* its security categories' */
    int32_t crit;                  /* open crits */
} jolly_alarm_obs_t;

typedef struct {
    bool seeded;                   /* a first glance came */
    jolly_alarm_obs_t last;
    int64_t until;                 /* alerta until then; reminders count from it */
} jolly_alarm_t;

void jolly_alarm_init(jolly_alarm_t *a);

/* A fresh glance at now_ms, with the configured alarm length (seconds). */
void jolly_alarm_feed(jolly_alarm_t *a, const jolly_alarm_obs_t *obs, int64_t now_ms, int32_t alarm_s);

/* JM_ALERTA while alarmed, JM_ESPIA or JM_SORPRENDIDO during a reminder,
 * else JM_TRANQUILO (nothing to show). */
jolly_mood_t jolly_alarm_mood(const jolly_alarm_t *a, int64_t now_ms);

/* Alarmed now (security wins over play only then). */
bool jolly_alarm_active(const jolly_alarm_t *a, int64_t now_ms);

bool jolly_alarm_s_ok(int32_t s);

#ifdef __cplusplus
}
#endif
