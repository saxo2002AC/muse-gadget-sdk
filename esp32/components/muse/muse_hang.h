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

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A hang watchdog for the silent freezes (no log, no heartbeat, no panic):
 * the UI's frame and Link's 5 s heartbeat kick it, and a high-priority task
 * with its stack in internal RAM checks them every second. One that stops for
 * longer than its limit gets every task's backtrace printed (esp_rom_printf,
 * so a held stdout lock can't stop it) and an abort, which reboots with the
 * panic's own backtrace. A task stuck with interrupts off is the interrupt
 * watchdog's (the ES3C40P's overlay turns it and the task watchdog on).
 */

typedef enum {
    MUSE_HANG_UI,                  /* muse_ui's frame timer, every frame */
    MUSE_HANG_APP,                 /* Link's heartbeat, every 5 s */
    MUSE_HANG_COUNT,
} muse_hang_id_t;

#define MUSE_HANG_UI_MS 20000      /* a dark screen still ticks, slower */
#define MUSE_HANG_APP_MS 90000     /* a heartbeat can wait on a VM connect and its retries */

/* Which watched clock stalled at now_ms, or -1. `last` 0: never kicked, not
 * watched yet. Pure: tests/jolly/test_hang.c. */
static inline int muse_hang_stalled(const int64_t last[MUSE_HANG_COUNT], int64_t now_ms)
{
    static const int64_t LIMIT[MUSE_HANG_COUNT] = { MUSE_HANG_UI_MS, MUSE_HANG_APP_MS };
    for (int i = 0; i < MUSE_HANG_COUNT; i++) {
        if (last[i] && now_ms - last[i] > LIMIT[i]) {
            return i;
        }
    }
    return -1;
}

/* Starts the watchdog task (once). */
void muse_hang_start(void);

/* This clock is alive. Cheap: one store. Any task. */
void muse_hang_kick(muse_hang_id_t id);

#ifdef __cplusplus
}
#endif
