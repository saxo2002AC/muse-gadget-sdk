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

#include "jolly/jolly_fast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "jolly/jolly_fast_auth.h"
#include "muse_url.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "jolly_fast";

#define NVS_NS "jolly_fast"
#ifndef CONFIG_MUSE_JOLLY_FAST_URL
#define CONFIG_MUSE_JOLLY_FAST_URL ""
#endif

static portMUX_TYPE s_init_mux = portMUX_INITIALIZER_UNLOCKED;
static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock;
static bool s_loaded;
static jolly_fast_cfg_t s_cfg;      /* the key's bytes in here: wiped copies only leave it */
static char s_last[48];             /* the last ask, for >fast: action, intent, time; never text */

static void lock(void)
{
    taskENTER_CRITICAL(&s_init_mux);
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    }
    taskEXIT_CRITICAL(&s_init_mux);
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

static void load_locked(void)
{
    if (s_loaded) {
        return;
    }
    s_loaded = true;
    s_cfg = (jolly_fast_cfg_t){ .max_s = 6, .ms = 1500 };
    strlcpy(s_cfg.url, CONFIG_MUSE_JOLLY_FAST_URL, sizeof(s_cfg.url));
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t on = 0;
    int32_t v;
    if (nvs_get_u8(h, "on", &on) == ESP_OK) {
        s_cfg.on = on == 1;
    }
    size_t n = sizeof(s_cfg.url);
    char url[sizeof(s_cfg.url)];
    if (nvs_get_str(h, "url", url, &n) == ESP_OK) {
        strlcpy(s_cfg.url, url, sizeof(s_cfg.url));
    }
    char hex[JOLLY_FAST_KEY_HEX + 1];
    n = sizeof(hex);
    if (nvs_get_str(h, "key", hex, &n) == ESP_OK) {
        s_cfg.have_key = jolly_unhex(hex, s_cfg.key, sizeof(s_cfg.key));
    }
    jolly_wipe(hex, sizeof(hex));
    if (nvs_get_i32(h, "max_s", &v) == ESP_OK && jolly_fast_max_s_ok(v)) {
        s_cfg.max_s = (int)v;
    }
    if (nvs_get_i32(h, "ms", &v) == ESP_OK && jolly_fast_ms_ok(v)) {
        s_cfg.ms = (int)v;
    }
    nvs_close(h);
}

/* One NVS write: a string, an i32 or a u8; an empty string erases. */
static esp_err_t save(const char *key, const char *str, int32_t num, bool is_u8)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    if (str) {
        err = str[0] ? nvs_set_str(h, key, str) : nvs_erase_key(h, key);
        err = err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
    } else {
        err = is_u8 ? nvs_set_u8(h, key, (uint8_t)num) : nvs_set_i32(h, key, num);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static void answer(const char *what, const char *err)
{
    if (err) {
        printf("@%s error %s\n", what, err);
    } else {
        printf("@%s ok\n", what);
    }
    fflush(stdout);
}

bool jolly_fast_config(jolly_fast_cfg_t *cfg)
{
    lock();
    load_locked();
    *cfg = s_cfg;
    unlock();
    muse_url_t u;
    bool ready = cfg->on && cfg->have_key && cfg->url[0] && muse_url_parse(cfg->url, &u);
    if (!ready) {
        jolly_wipe(cfg->key, sizeof(cfg->key));
    }
    return ready;
}

void jolly_fast_note_last(const char *summary)
{
    lock();
    strlcpy(s_last, summary, sizeof(s_last));
    unlock();
}

/* A number in [lo, hi], whole digits only. */
static bool number(const char *v, long lo, long hi, long *out)
{
    char *end = NULL;
    long n = strtol(v, &end, 10);
    if (!*v || *end || strlen(v) > 5 || n < lo || n > hi) {
        return false;
    }
    *out = n;
    return true;
}

bool jolly_fast_console_command(const char *line)
{
    long n;
    if (!strncmp(line, "fast_url=", 9)) {
        const char *v = line + 9;
        muse_url_t u;
        if (v[0] && (strlen(v) >= sizeof(s_cfg.url) || !muse_url_parse(v, &u))) {
            answer("fast_url", "invalid (want http://IPv4:port or http://name.local:port)");
            return true;
        }
        lock();
        load_locked();
        esp_err_t err = save("url", v, 0, false);
        if (err == ESP_OK) {
            strlcpy(s_cfg.url, v[0] ? v : CONFIG_MUSE_JOLLY_FAST_URL, sizeof(s_cfg.url));
        }
        unlock();
        answer("fast_url", err == ESP_OK ? NULL : "storage");
        return true;
    }
    if (!strncmp(line, "fast_key=", 9)) {
        const char *v = line + 9;
        if (v[0] && !jolly_fast_key_ok(v)) {
            answer("fast_key", "invalid (want 64 hex digits)");
            return true;
        }
        lock();
        load_locked();
        esp_err_t err = save("key", v, 0, false);
        if (err == ESP_OK) {
            s_cfg.have_key = v[0] && jolly_unhex(v, s_cfg.key, sizeof(s_cfg.key));
            if (!s_cfg.have_key) {
                jolly_wipe(s_cfg.key, sizeof(s_cfg.key));
            }
        }
        unlock();
        ESP_LOGI(TAG, "atajo key %s", v[0] ? "set" : "cleared");   /* never the key */
        answer("fast_key", err == ESP_OK ? NULL : "storage");
        return true;
    }
    if (!strncmp(line, "fast=", 5)) {
        if (!number(line + 5, 0, 1, &n)) {
            answer("fast", "invalid (want 0 or 1)");
            return true;
        }
        lock();
        load_locked();
        esp_err_t err = save("on", NULL, (int32_t)n, true);
        if (err == ESP_OK) {
            s_cfg.on = n == 1;
        }
        unlock();
        ESP_LOGI(TAG, "atajo %s", n ? "on" : "off");
        answer("fast", err == ESP_OK ? NULL : "storage");
        return true;
    }
    if (!strncmp(line, "fast_max_s=", 11) || !strncmp(line, "fast_ms=", 8)) {
        bool max_s = line[5] == 'm' && line[6] == 'a';
        const char *what = max_s ? "fast_max_s" : "fast_ms";
        const char *v = strchr(line, '=') + 1;
        if (max_s ? !number(v, 2, 15, &n) : !number(v, 800, 3000, &n)) {
            answer(what, max_s ? "invalid (want 2..15)" : "invalid (want 800..3000)");
            return true;
        }
        lock();
        load_locked();
        esp_err_t err = save(max_s ? "max_s" : "ms", NULL, (int32_t)n, false);
        if (err == ESP_OK) {
            *(max_s ? &s_cfg.max_s : &s_cfg.ms) = (int)n;
        }
        unlock();
        answer(what, err == ESP_OK ? NULL : "storage");
        return true;
    }
    if (!strncmp(line, "fast_log=", 9)) {
        if (!number(line + 9, 0, 1, &n)) {
            answer("fast_log", "invalid (want 0 or 1)");
            return true;
        }
        lock();
        load_locked();
        s_cfg.log = n == 1;   /* RAM only: off again at boot */
        unlock();
        answer("fast_log", NULL);
        return true;
    }
    if (!strcmp(line, "fast")) {
        lock();
        load_locked();
        char url[sizeof(s_cfg.url)], last[sizeof(s_last)];
        strlcpy(url, s_cfg.url, sizeof(url));
        strlcpy(last, s_last, sizeof(last));
        int on = s_cfg.on, max_s = s_cfg.max_s, ms = s_cfg.ms, log = s_cfg.log;
        unsigned key_len = s_cfg.have_key ? JOLLY_FAST_KEY_HEX : 0;
        unlock();
        printf("@fast {\"on\":%d,\"url\":\"%s\",\"key_len\":%u,\"max_s\":%d,\"ms\":%d,\"log\":%d,\"breaker\":\"%s\","
               "\"last\":\"%s\"}\n",
               on, url, key_len, max_s, ms, log, jolly_fast_breaker_name(), last);
        fflush(stdout);
        return true;
    }
    return false;
}
