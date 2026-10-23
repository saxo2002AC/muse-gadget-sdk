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

#include "jolly/jolly_fast_parse.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * One /v1/ask at a time, on a worker task (PSRAM stack), so the voice task
 * keeps playing Muse's reply meanwhile (parallel mode). The recording is
 * copied, so the voice task may record again at once. The deadline is
 * fast_ms from the call; past it the answer no longer counts, whatever
 * arrives. Errors (and late answers) feed the breaker; a cancel doesn't.
 */

typedef enum {
    JFA_IDLE,        /* nothing asked */
    JFA_PENDING,     /* waiting, within the deadline */
    JFA_LOCAL,       /* answered: speak it (*out has it) */
    JFA_FORWARD,     /* answered: Muse's */
    JFA_ERROR,       /* no valid answer in time: Muse's */
} jolly_fast_state_t;

/* Starts asking about this recording. False (nothing asked) if the atajo is
 * off or unset, the breaker is open, there's no synced clock, the note is
 * longer than fast_max_s or shorter than the contract's 0.3 s, or an ask
 * is still running. */
bool jolly_fast_ask_start(const int16_t *pcm, size_t frames);

/* Without waiting: where the ask is. JFA_LOCAL fills *out. */
jolly_fast_state_t jolly_fast_ask_poll(jolly_fast_answer_t *out);

/* Its answer no longer matters (a new press): not a failure. */
void jolly_fast_ask_cancel(void);

#ifdef __cplusplus
}
#endif
