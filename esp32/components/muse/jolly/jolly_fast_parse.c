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

#include "jolly/jolly_fast_parse.h"

#include <string.h>

#include "cJSON.h"

/* Next code point of valid UTF-8 (no overlongs, surrogates or > U+10FFFF);
 * -1 if invalid. */
static int32_t next_cp(const unsigned char **p, const unsigned char *end)
{
    const unsigned char *s = *p;
    unsigned c = *s;
    int n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
    if (!n || s + n > end) {
        return -1;
    }
    int32_t cp = n == 1 ? (int32_t)c : n == 2 ? (int32_t)(c & 0x1F) : n == 3 ? (int32_t)(c & 0x0F) : (int32_t)(c & 0x07);
    for (int i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            return -1;
        }
        cp = cp << 6 | (s[i] & 0x3F);
    }
    static const int32_t MIN[5] = { 0, 0, 0x80, 0x800, 0x10000 };
    if (cp < MIN[n] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return -1;
    }
    *p = s + n;
    return cp;
}

/* Format characters (bidi, zero-width, invisible operators, tags). */
static bool format_char(int32_t c)
{
    return c == 0xAD || (c >= 0x600 && c <= 0x605) || c == 0x61C || c == 0x6DD || c == 0x70F || c == 0x180E ||
           (c >= 0x200B && c <= 0x200F) || (c >= 0x202A && c <= 0x202E) || (c >= 0x2060 && c <= 0x2064) ||
           (c >= 0x2066 && c <= 0x206F) || c == 0xFEFF || (c >= 0xFFF9 && c <= 0xFFFB) || c == 0xE0001 ||
           (c >= 0xE0020 && c <= 0xE007F);
}

/* Valid UTF-8; for text to show and speak, none of the characters above,
 * controls, C1 or private use. */
static bool clean(const char *s, bool shown)
{
    const unsigned char *p = (const unsigned char *)s, *end = p + strlen(s);
    while (p < end) {
        int32_t c = next_cp(&p, end);
        if (c < 0) {
            return false;
        }
        if (shown && (c < 0x20 || c == 0x7F || (c >= 0x80 && c <= 0x9F) || format_char(c) ||
                      (c >= 0xE000 && c <= 0xF8FF) || c >= 0xF0000)) {
            return false;
        }
    }
    return true;
}

static const char *str(cJSON *o, const char *key, size_t max)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    const char *s = cJSON_IsString(v) ? v->valuestring : NULL;
    return s && strlen(s) <= max ? s : NULL;
}

static bool num(cJSON *o, const char *key, int32_t lo, int32_t hi, int32_t *out)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(v) || v->valuedouble != (double)(int32_t)v->valuedouble || v->valuedouble < lo ||
        v->valuedouble > hi) {
        return false;
    }
    *out = (int32_t)v->valuedouble;
    return true;
}

static bool one_of(const char *s, const char *const *set)
{
    for (; *set; set++) {
        if (!strcmp(s, *set)) {
            return true;
        }
    }
    return false;
}

bool jolly_fast_parse(const char *json, size_t len, jolly_fast_answer_t *out)
{
    static const char *const INTENTS[] = { "hora", "fecha", "estado", "apps", "seguridad", "respaldo", "clima", NULL };
    static const char *const REASONS[] = { "no_match", "no_speech", "low_confidence", "other_language", "slow",
                                           "data_unavailable", "disabled", "error", NULL };
    memset(out, 0, sizeof(*out));
    if (!json || !len || len > JOLLY_FAST_BODY_MAX) {
        return false;
    }
    cJSON *o = cJSON_ParseWithLength(json, len);
    bool ok = false;
    int32_t v;
    const char *action, *intent, *text, *reason, *transcript;
    if (!cJSON_IsObject(o) || !num(o, "v", 1, 1, &v) || !(action = str(o, "action", 16)) ||
        !(intent = str(o, "intent", sizeof(out->intent) - 1)) || !(text = str(o, "text", JOLLY_FAST_TEXT_MAX)) ||
        !(reason = str(o, "reason", sizeof(out->reason) - 1)) ||
        !(transcript = str(o, "transcript", JOLLY_FAST_TRANSCRIPT_MAX)) || !num(o, "stt_ms", 0, 60000, &out->stt_ms) ||
        !num(o, "total_ms", 0, 60000, &out->total_ms) || !clean(transcript, true)) {
        goto done;
    }
    if (!strcmp(action, "local")) {
        if (!one_of(intent, INTENTS) || !text[0] || !clean(text, true) || reason[0] || transcript[0]) {   /* C14 */
            goto done;
        }
        out->local = true;
    } else if (!strcmp(action, "forward")) {
        if (intent[0] || text[0] || !one_of(reason, REASONS)) {
            goto done;
        }
    } else {
        goto done;
    }
    strcpy(out->intent, intent);
    strcpy(out->text, text);
    strcpy(out->reason, reason);
    strcpy(out->transcript, transcript);
    ok = true;
done:
    cJSON_Delete(o);
    if (!ok) {
        memset(out, 0, sizeof(*out));
    }
    return ok;
}

void jolly_fast_breaker_init(jolly_fast_breaker_t *b)
{
    b->fails = 0;
    b->open_until = 0;
}

bool jolly_fast_breaker_closed(const jolly_fast_breaker_t *b, int64_t now)
{
    return !b->open_until || now >= b->open_until;
}

void jolly_fast_breaker_result(jolly_fast_breaker_t *b, bool ok, int64_t now)
{
    if (ok) {
        b->fails = 0;
        b->open_until = 0;
        return;
    }
    /* A failure on the first try after the 5 minutes opens it again at once. */
    b->fails = b->open_until ? 2 : b->fails + 1;
    if (b->fails >= 2) {
        b->open_until = now + JOLLY_FAST_OPEN_MS;
    }
}
