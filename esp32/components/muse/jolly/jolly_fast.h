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
 * The atajo rápido (warroom plan-atajo-rapido.md, workstream B), in parallel
 * mode (Andrés's D1): the note still streams to Muse live, as always; on
 * release the recording also goes to the Mini's jolly-atajo /v1/ask. If it
 * answers "local" within fast_ms, Jolly speaks that and Muse's turn is
 * cancelled and its reply suppressed; else Muse's turn goes on untouched.
 *
 * Config, in NVS (namespace jolly_fast), serial console only (the "fast"
 * family is console-only: muse_secret.c, Diana C10):
 *   >fast_url=http://Mac-mini-de-acserver.local:PORT   (or IPv4; empty clears)
 *   >fast_key=<64 hex>          never printed; it's never sent either (jolly_fast_auth.h)
 *   >fast=0|1                   off by default until Diana's re-check
 *   >fast_max_s=2..15 (6)       longer notes aren't asked about
 *   >fast_ms=800..3000 (1500)   the answer's deadline from release
 *   >fast_log=0|1               transcripts and answers in the log; RAM only, off at boot
 *   >fast                       @fast {on, url, key_len, max_s, ms, breaker, last}
 */

typedef struct {
    bool on;
    char url[96];
    uint8_t key[32];
    bool have_key;
    int max_s;
    int ms;
    bool log;
} jolly_fast_cfg_t;

/* A copy of the config; the caller wipes cfg->key when done. False if the
 * atajo is off or not set up (no URL or key). */
bool jolly_fast_config(jolly_fast_cfg_t *cfg);

/* What the last ask came to, for >fast ("local hora 640ms"): never its text. */
void jolly_fast_note_last(const char *summary);

/* "closed" or "open" (jolly_fast_ask.c's breaker). */
const char *jolly_fast_breaker_name(void);

/* Handles a console line without its '>'; false if it isn't one of these. */
bool jolly_fast_console_command(const char *line);

#ifdef __cplusplus
}
#endif
