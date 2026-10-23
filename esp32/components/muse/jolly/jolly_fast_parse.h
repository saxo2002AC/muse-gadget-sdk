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
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The atajo's /v1/ask answer, parsed strictly (contract v1.1,
 * tests/jolly/contract/atajo/README.md; Diana C13): at most 1024 bytes, all
 * 8 keys with the right types, v 1, known enums, local with a 1..280-byte
 * text of valid UTF-8 with no controls, C1, private-use or format (bidi,
 * zero-width) characters; forward with none and a reason. Extra keys are
 * ignored. Anything else fails, which is an error (Muse goes on). And the
 * breaker: two failures in a row skip the atajo for 5 minutes. Pure:
 * tests/jolly/test_fast.c.
 */

#define JOLLY_FAST_BODY_MAX 1024
#define JOLLY_FAST_TEXT_MAX 280
#define JOLLY_FAST_TRANSCRIPT_MAX 400
#define JOLLY_FAST_OPEN_MS 300000   /* the breaker, open */

typedef struct {
    bool local;
    char intent[12];
    char text[JOLLY_FAST_TEXT_MAX + 1];
    char reason[20];
    char transcript[JOLLY_FAST_TRANSCRIPT_MAX + 1];   /* logged only with >fast_log=1, never shown */
    int32_t stt_ms, total_ms;
} jolly_fast_answer_t;

bool jolly_fast_parse(const char *json, size_t len, jolly_fast_answer_t *out);

typedef struct {
    int fails;                     /* in a row */
    int64_t open_until;            /* ms; 0: closed */
} jolly_fast_breaker_t;

void jolly_fast_breaker_init(jolly_fast_breaker_t *b);

/* The atajo may be asked now. */
bool jolly_fast_breaker_closed(const jolly_fast_breaker_t *b, int64_t now_ms);

/* An ask came to a valid answer (ok, local or forward) or an error. */
void jolly_fast_breaker_result(jolly_fast_breaker_t *b, bool ok, int64_t now_ms);

#ifdef __cplusplus
}
#endif
