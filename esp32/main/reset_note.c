// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "reset_note.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "reset_note_text.h"

static const char *TAG = "link.reset_note";
static const char *NS = "resetnote";   // its own: config_clear_setup() never touches it

typedef struct {
    reset_note_t last[RESET_NOTE_KEEP];
    uint32_t count;
} notes_t;

static SemaphoreHandle_t s_lock;
static StaticSemaphore_t s_lock_buf;

static void lock(void) {
    // Called from several tasks; created on first use (the scheduler is up).
    static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);
    if (!s_lock) s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    portEXIT_CRITICAL(&mux);
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void) {
    xSemaphoreGive(s_lock);
}

static void load(notes_t *n) {
    memset(n, 0, sizeof(*n));
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(*n);
    if (nvs_get_blob(h, "notes", n, &len) != ESP_OK || len != sizeof(*n)) memset(n, 0, sizeof(*n));
    nvs_close(h);
    for (int i = 0; i < RESET_NOTE_KEEP; i++) n->last[i].source[RESET_NOTE_MAX - 1] = '\0';
}

void reset_note(const char *fmt, ...) {
    char source[RESET_NOTE_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(source, sizeof(source), fmt, ap);
    va_end(ap);
    uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000);
    ESP_LOGW(TAG, "SETUP RESET SOURCE: %s (uptime %u s)", source, (unsigned)up);
    lock();
    notes_t *n = malloc(sizeof(*n));   // ~280 bytes: not on a small task's stack
    if (n) {
        load(n);
        reset_note_push(n->last, source, up);
        n->count++;
        nvs_handle_t h;
        if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
            if (nvs_set_blob(h, "notes", n, sizeof(*n)) != ESP_OK || nvs_commit(h) != ESP_OK) {
                ESP_LOGE(TAG, "couldn't keep the reset source");
            }
            nvs_close(h);
        }
        free(n);
    }
    unlock();
}

bool reset_note_console_command(const char *line) {
    if (strcmp(line, "whyreset") != 0) return false;
    lock();
    notes_t *n = malloc(sizeof(*n));
    char *out = malloc(512);
    if (n && out) {
        load(n);
        reset_note_json(out, 512, n->last, n->count);
        printf("%s\n", out);
    } else {
        printf("@error whyreset: out of memory\n");
    }
    fflush(stdout);
    free(out);
    free(n);
    unlock();
    return true;
}
