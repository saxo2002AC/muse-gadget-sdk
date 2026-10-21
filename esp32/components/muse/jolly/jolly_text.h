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

#include <stddef.h>
#include <stdint.h>

#include "jolly/jolly_glance.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The Spanish strings Jolly's status pages show. Pure, so
 * tests/jolly/test_text.c checks them on the host. Times are differences of
 * the Guardián's own timestamps: the board keeps no wall clock.
 */

typedef enum { JTONE_OK, JTONE_WARN, JTONE_CRIT, JTONE_STALE } jolly_tone_t;

/* "hace 3 s", "hace 2 min", "hace 2 h", "hace 1 día", "hace 10 días";
 * "ahora" for the future and "—" for JOLLY_NONE. */
void jolly_fmt_ago(int64_t secs, char *out, size_t cap);

/* Thousandths as one decimal: kbps as Mb/s, MB as GB. "—" if negative. */
void jolly_fmt_tenths(int32_t thousandths, char *out, size_t cap);

/* Megabytes as gigabytes of 1024, one decimal: swap as macOS shows it. */
void jolly_fmt_gib(int32_t mb, char *out, size_t cap);

/* The status bar's chip (§1.8): the link's trouble, else the alerts, else
 * "Mac Mini OK"; `glance` is NULL before the first. *tone colours it. */
void jolly_fmt_chip(jolly_link_t link, int auth_status, const jolly_glance_t *glance, char *out, size_t cap,
                    jolly_tone_t *tone);

const char *jolly_word_level(jolly_level_t level);   /* memory: Normal, Presión, Crítica */
const char *jolly_word_therm(jolly_therm_t therm);   /* normal, alta, crítica */
const char *jolly_word_sev(jolly_level_t sev);       /* info, aviso, crítico */

#ifdef __cplusplus
}
#endif
