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

#include "jolly/jolly_text.h"

#include <stdio.h>

void jolly_fmt_ago(int64_t secs, char *out, size_t cap)
{
    if (secs == JOLLY_NONE) {
        snprintf(out, cap, "—");
    } else if (secs < 0) {
        snprintf(out, cap, "ahora");
    } else if (secs < 60) {
        snprintf(out, cap, "hace %d s", (int)secs);
    } else if (secs < 3600) {
        snprintf(out, cap, "hace %d min", (int)(secs / 60));
    } else if (secs < 86400) {
        snprintf(out, cap, "hace %d h", (int)(secs / 3600));
    } else {
        int days = (int)(secs / 86400);
        snprintf(out, cap, "hace %d %s", days, days == 1 ? "día" : "días");
    }
}

void jolly_fmt_tenths(int32_t thousandths, char *out, size_t cap)
{
    if (thousandths < 0) {
        snprintf(out, cap, "—");
        return;
    }
    int32_t tenths = (thousandths + 50) / 100;
    snprintf(out, cap, "%d.%d", (int)(tenths / 10), (int)(tenths % 10));
}

void jolly_fmt_gib(int32_t mb, char *out, size_t cap)
{
    if (mb < 0) {
        snprintf(out, cap, "—");
        return;
    }
    int32_t tenths = (int32_t)(((int64_t)mb * 10 + 512) / 1024);
    snprintf(out, cap, "%d.%d", (int)(tenths / 10), (int)(tenths % 10));
}

void jolly_fmt_chip(jolly_link_t link, int auth_status, const jolly_glance_t *glance, char *out, size_t cap,
                    jolly_tone_t *tone)
{
    *tone = JTONE_STALE;
    switch (link) {
    case JL_WIFI_DOWN:
        snprintf(out, cap, "Sin WiFi");
        return;
    case JL_UNREACHABLE:
        snprintf(out, cap, "Sin conexión con el Mac Mini");
        return;
    case JL_UNAUTH:
        snprintf(out, cap, auth_status == 403 ? "IP no permitida" : "Clave inválida");
        return;
    case JL_STALE:
        snprintf(out, cap, "Sin datos");
        return;
    case JL_OK:
        break;
    }
    if (!glance) {
        snprintf(out, cap, "Sin datos");
    } else if (glance->crit > 0) {
        *tone = JTONE_CRIT;
        snprintf(out, cap, "%d %s", (int)glance->crit, glance->crit == 1 ? "crítica" : "críticas");
    } else if (glance->warn > 0) {
        *tone = JTONE_WARN;
        snprintf(out, cap, "%d %s", (int)glance->warn, glance->warn == 1 ? "aviso" : "avisos");
    } else {
        *tone = JTONE_OK;
        snprintf(out, cap, "Mac Mini OK");
    }
}

const char *jolly_word_level(jolly_level_t level)
{
    return level == JLV_CRIT ? "Crítica" : level == JLV_WARN ? "Presión" : "Normal";
}

const char *jolly_word_therm(jolly_therm_t therm)
{
    return therm == JT_CRITICO ? "crítica" : therm == JT_ALTO ? "alta" : "normal";
}

const char *jolly_word_sev(jolly_level_t sev)
{
    return sev == JLV_CRIT ? "crítico" : sev == JLV_WARN ? "aviso" : "info";
}

void jolly_fmt_clock(bool synced, int hour, int min, bool h12, char *out, size_t cap)
{
    if (!synced || hour < 0 || hour > 23 || min < 0 || min > 59) {
        snprintf(out, cap, "--:--");
    } else if (h12) {
        snprintf(out, cap, "%d:%02d %s", hour % 12 ? hour % 12 : 12, min, hour < 12 ? "am" : "pm");
    } else {
        snprintf(out, cap, "%02d:%02d", hour, min);
    }
}
