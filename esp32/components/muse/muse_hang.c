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

#include "muse_hang.h"

#include <stdatomic.h>

#include "esp_debug_helpers.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "muse_mem.h"

static _Atomic int64_t s_last[MUSE_HANG_COUNT];
static TaskHandle_t s_task;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

void muse_hang_kick(muse_hang_id_t id)
{
    if ((unsigned)id < MUSE_HANG_COUNT) {
        atomic_store_explicit(&s_last[id], now_ms(), memory_order_relaxed);
    }
}

static void hang_task(void *arg)
{
    (void)arg;
    static const char *const NAMES[MUSE_HANG_COUNT] = { "UI frame", "Link heartbeat" };
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        int64_t last[MUSE_HANG_COUNT];
        for (int i = 0; i < MUSE_HANG_COUNT; i++) {
            last[i] = atomic_load_explicit(&s_last[i], memory_order_relaxed);
        }
        int64_t now = now_ms();
        int stalled = muse_hang_stalled(last, now);
        if (stalled < 0) {
            continue;
        }
        /* The ROM printer: a task stuck holding stdout's lock can't stop it. */
        esp_rom_printf("\n*** HANG: %s stalled for %d ms; every task's backtrace follows ***\n", NAMES[stalled],
                       (int)(now - last[stalled]));
        esp_backtrace_print_all_tasks(24);
        esp_system_abort("muse_hang: a watched task stopped (see the backtraces above)");
    }
}

void muse_hang_start(void)
{
    if (s_task) {
        return;
    }
    /* Above every app task, so one spinning at high priority can't starve it,
     * and below the system's. Its stack in PSRAM, as the other Muse tasks':
     * internal RAM is short. A hang with the caches off (a stuck flash write)
     * is the interrupt watchdog's. */
    if (xTaskCreatePinnedToCoreWithCaps(hang_task, "muse_hang", 4096, NULL, configMAX_PRIORITIES - 4, &s_task,
                                        tskNO_AFFINITY, MUSE_BIG_CAPS) != pdPASS) {
        s_task = NULL;
    }
}
