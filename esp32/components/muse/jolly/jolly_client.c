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

#include "jolly/jolly_client.h"
#include "jolly/jolly_alarm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#include "muse_mem.h"
#include "muse_resolve.h"
#include "muse_ui.h"
#include "muse_url.h"
#include "muse_wifi.h"

static const char *TAG = "jolly_grd";

#define NVS_NS "jolly_grd"
#define URL_MAX 96                 /* http://host:port (muse_url.h) */
#define KEY_MAX 96                 /* the key is >= 32 random bytes, base64url */
#define POLL_MS 3000               /* the glance, with the screen on */
#define POLL_DARK_MS 10000         /* ... and off */
#define PAGE_MS 10000              /* apps, events and the 10-minute series */
#define DAY_MS (5 * 60 * 1000)     /* the 24-hour series */
#define TIMEOUT_MS 3000            /* connect, headers, each read */
#define BODY_MAX JOLLY_EVENTS_MAX  /* the largest endpoint cap */
#define ERR_LOG_MAX 64             /* bytes of an error body in the log (R16) */

static portMUX_TYPE s_init_mux = portMUX_INITIALIZER_UNLOCKED;
static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock;   /* everything below */
static bool s_loaded;
EXT_RAM_BSS_ATTR static char s_url[URL_MAX];
EXT_RAM_BSS_ATTR static char s_key[KEY_MAX];
static int32_t s_alarm_s = JOLLY_ALARM_DEFAULT_S;   /* >grd_alarm_s= */

static TaskHandle_t s_task;
static char *s_body;               /* BODY_MAX + 1, PSRAM */
EXT_RAM_BSS_ATTR static jolly_glance_t s_glance;
EXT_RAM_BSS_ATTR static jolly_apps_t s_apps;
EXT_RAM_BSS_ATTR static jolly_events_t s_events;
EXT_RAM_BSS_ATTR static jolly_series_t s_series[2];   /* 10m, 24h */
static bool s_have_glance, s_have_apps, s_have_events, s_have_series[2];
static jolly_linkstate_t s_link;
static unsigned s_want;

/* One request at a time, on the poll task: its response headers land here. */
static char s_content_type[48];
static char s_etag_in[40];

/* ---- Settings ---- */

static void lock(void)
{
    taskENTER_CRITICAL(&s_init_mux);
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
        jolly_link_init(&s_link);
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
    nvs_handle_t h;
    s_url[0] = s_key[0] = '\0';
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof(s_url);
        if (nvs_get_str(h, "url", s_url, &n) != ESP_OK) {
            s_url[0] = '\0';
        }
        n = sizeof(s_key);
        if (nvs_get_str(h, "key", s_key, &n) != ESP_OK) {
            s_key[0] = '\0';
        }
        int32_t a;
        s_alarm_s = nvs_get_i32(h, "alarm_s", &a) == ESP_OK && jolly_alarm_s_ok(a) ? a : JOLLY_ALARM_DEFAULT_S;
        nvs_close(h);
    }
    s_loaded = true;
}

static esp_err_t save(const char *key, const char *value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    if (value[0]) {
        err = nvs_set_str(h, key, value);
    } else {
        err = nvs_erase_key(h, key);
        err = err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
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

static const char *link_name(jolly_link_t l)
{
    static const char *const NAMES[] = { "ok", "wifi_down", "unreachable", "unauth", "stale" };
    return l <= JL_STALE ? NAMES[l] : "?";
}

/* A new URL or key: forget what the old server said and poll now. */
static void changed_locked(void)
{
    s_have_glance = s_have_apps = s_have_events = s_have_series[0] = s_have_series[1] = false;
    jolly_link_init(&s_link);
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}

bool jolly_client_console_command(const char *line)
{
    if (!strncmp(line, "grd_url=", 8)) {
        const char *v = line + 8;
        muse_url_t u;
        if (v[0] && (strlen(v) >= URL_MAX || !muse_url_parse(v, &u))) {
            answer("grd_url", "invalid (want http://IPv4:port or http://name.local:port)");
            return true;
        }
        lock();
        load_locked();
        esp_err_t err = save("url", v);
        if (err == ESP_OK) {
            strlcpy(s_url, v, sizeof(s_url));
            changed_locked();
        }
        unlock();
        if (err != ESP_OK) {
            answer("grd_url", "storage");
        } else {
            ESP_LOGI(TAG, "Guardián %s", v[0] ? v : "cleared");
            answer("grd_url", NULL);
        }
        return true;
    }
    if (!strncmp(line, "grd_key=", 8)) {
        const char *v = line + 8;
        size_t len = strlen(v);
        if (len >= KEY_MAX) {
            answer("grd_key", "too long");
            return true;
        }
        lock();
        load_locked();
        esp_err_t err = save("key", v);
        if (err == ESP_OK) {
            strlcpy(s_key, v, sizeof(s_key));
            changed_locked();
        }
        unlock();
        if (err != ESP_OK) {
            answer("grd_key", "storage");
        } else {
            ESP_LOGI(TAG, "Guardián key %s (%u chars)", len ? "set" : "cleared", (unsigned)len);   /* never the key */
            answer("grd_key", NULL);
        }
        return true;
    }
    if (!strncmp(line, "grd_alarm_s=", 12)) {
        /* How long a new crit keeps Jolly alarmed, 0..3600 s (jolly_alarm.h). */
        const char *v = line + 12;
        char *end = NULL;
        long s = strtol(v, &end, 10);
        if (!*v || *end || !jolly_alarm_s_ok((int32_t)s) || strlen(v) > 4) {
            answer("grd_alarm_s", "invalid (want 0..3600)");
            return true;
        }
        lock();
        load_locked();
        nvs_handle_t h;
        esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
        if (err == ESP_OK) {
            err = nvs_set_i32(h, "alarm_s", (int32_t)s);
            err = err == ESP_OK ? nvs_commit(h) : err;
            nvs_close(h);
        }
        if (err == ESP_OK) {
            s_alarm_s = (int32_t)s;
        }
        unlock();
        answer("grd_alarm_s", err == ESP_OK ? NULL : "storage");
        return true;
    }
    if (!strcmp(line, "grd")) {
        lock();
        load_locked();
        printf("@grd {\"url\":\"%s\",\"key_len\":%u,\"link\":\"%s\",\"glance\":%s,\"alarm_s\":%d}\n", s_url,
               (unsigned)strlen(s_key), link_name(s_link.link), s_have_glance ? "true" : "false", (int)s_alarm_s);
        unlock();
        fflush(stdout);
        return true;
    }
    return false;
}

/* ---- One request ---- */

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_HEADER || !evt->header_key || !evt->header_value) {
        return ESP_OK;
    }
    if (!strcasecmp(evt->header_key, "Content-Type")) {
        strlcpy(s_content_type, evt->header_value, sizeof(s_content_type));
    } else if (!strcasecmp(evt->header_key, "ETag")) {
        strlcpy(s_etag_in, evt->header_value, sizeof(s_etag_in));
    }
    return ESP_OK;
}

/*
 * GET `path` from the Guardián. Returns the HTTP status, or 0 when there's no
 * usable answer: no server, no connection, a body over `cap` or not JSON.
 * A 200's body is in s_body (NUL-terminated, *len bytes); its ETag, if it's
 * fit to send back, in etag_out (else empty).
 */
static int get(const char *path, size_t cap, const char *etag, char etag_out[33], size_t *len)
{
    char saved[URL_MAX], key[KEY_MAX];
    lock();
    strlcpy(saved, s_url, sizeof(saved));
    strlcpy(key, s_key, sizeof(key));
    unlock();
    etag_out[0] = '\0';
    *len = 0;
    muse_url_t server;
    char ip[16];
    if (!muse_url_parse(saved, &server) || !muse_resolve(&server, ip)) {
        memset(key, 0, sizeof(key));
        return 0;
    }
    char url[64 + 48];
    snprintf(url, sizeof(url), "http://%s:%d%s", ip, server.port, path);
    s_content_type[0] = s_etag_in[0] = '\0';
    const esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = TIMEOUT_MS,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
        .disable_auto_redirect = true,   /* R15 */
        .event_handler = on_http_event,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        memset(key, 0, sizeof(key));
        return 0;
    }
    if (key[0]) {
        esp_http_client_set_header(c, "X-Guardian-Key", key);
    }
    memset(key, 0, sizeof(key));
    if (etag && jolly_etag_ok(etag)) {
        esp_http_client_set_header(c, "If-None-Match", etag);
    }
    int status = 0;
    if (esp_http_client_open(c, 0) != ESP_OK) {
        muse_resolve_forget(&server);   /* a .local server may have moved */
        goto done;
    }
    int64_t length = esp_http_client_fetch_headers(c);
    status = esp_http_client_get_status_code(c);
    if (status != 200) {
        if (status != 304) {
            /* At most 64 bytes of the error, printable ASCII only (R16). */
            char body[ERR_LOG_MAX + 1];
            int n = esp_http_client_read(c, body, ERR_LOG_MAX);
            n = n > 0 ? n : 0;
            for (int i = 0; i < n; i++) {
                body[i] = body[i] < 0x20 || body[i] > 0x7e ? '.' : body[i];
            }
            body[n] = '\0';
            ESP_LOGD(TAG, "%s: %d %s", path, status, body);
        }
        goto done;
    }
    if (strncasecmp(s_content_type, "application/json", 16) != 0 || length > (int64_t)cap) {
        status = 0;   /* not JSON, or over the endpoint's cap: a failed poll */
        goto done;
    }
    size_t got = 0;
    for (;;) {
        /* Read one past the cap: a chunked body that runs over it fails. */
        int n = esp_http_client_read(c, s_body + got, (int)(cap + 1 - got));
        if (n < 0) {
            status = 0;
            goto done;
        }
        if (n == 0) {
            break;
        }
        got += (size_t)n;
        if (got > cap) {
            status = 0;
            goto done;
        }
    }
    if (!esp_http_client_is_complete_data_received(c)) {
        status = 0;
        goto done;
    }
    s_body[got] = '\0';
    *len = got;
    if (jolly_etag_ok(s_etag_in)) {
        strlcpy(etag_out, s_etag_in, 33);
    }
done:
    esp_http_client_cleanup(c);
    return status;
}

/* ---- The poll task ---- */

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void poll_glance(char etag[33])
{
    char next[33];
    size_t len;
    int status = muse_wifi_connected() ? get("/v1/glance", JOLLY_GLANCE_MAX, etag, next, &len) : 0;
    jolly_glance_t g;
    if (status == 200) {
        if (jolly_glance_parse(s_body, len, &g)) {
            strlcpy(etag, next, 33);
        } else {
            status = 0;   /* a body that doesn't parse is a failed poll */
            etag[0] = '\0';
        }
    }
    lock();
    jolly_link_t was = s_link.link;
    if (status == 200) {
        s_glance = g;
        s_have_glance = true;
    }
    jolly_link_on_result(&s_link, status, muse_wifi_connected(), s_have_glance ? s_glance.age_s : 0, now_ms());
    jolly_link_t is = s_link.link;
    unlock();
    if (is != was) {
        ESP_LOGI(TAG, "link %s (%d)", link_name(is), status);
    }
}

/* An optional endpoint, if it's wanted and due. */
static void poll_page(unsigned want, unsigned bit, const char *path, size_t cap, int64_t every_ms, int64_t *last,
                      char etag[33])
{
    if (!(want & bit)) {
        etag[0] = '\0';
        *last = 0;
        return;
    }
    int64_t now = now_ms();
    if (*last && now - *last < every_ms) {
        return;
    }
    *last = now;
    char next[33];
    size_t len;
    if (get(path, cap, etag, next, &len) != 200) {
        return;   /* 304 keeps what's there; failures show in the glance's link */
    }
    bool ok = false;
    lock();
    if (bit == JOLLY_WANT_APPS) {
        ok = s_have_apps = jolly_apps_parse(s_body, len, &s_apps);
    } else if (bit == JOLLY_WANT_EVENTS) {
        ok = s_have_events = jolly_events_parse(s_body, len, &s_events);
    } else {
        int i = bit == JOLLY_WANT_SERIES_24H;
        ok = s_have_series[i] = jolly_series_parse(s_body, len, &s_series[i]);
    }
    unlock();
    if (ok) {
        strlcpy(etag, next, 33);
    } else {
        etag[0] = '\0';
    }
}

static void poll_task(void *arg)
{
    (void)arg;
    char glance_etag[33] = "", etags[4][33] = { "", "", "", "" };
    int64_t last[4] = { 0, 0, 0, 0 };
    bool told = false;
    for (;;) {
        lock();
        bool configured = s_url[0] != '\0';
        unsigned want = s_want;
        unlock();
        if (!configured) {
            if (!told) {
                ESP_LOGI(TAG, "no Guardián set: >grd_url= and >grd_key= on the console");
                told = true;
            }
        } else {
            told = false;
            poll_glance(glance_etag);
            lock();
            bool linked = s_link.link == JL_OK || s_link.link == JL_STALE;
            unlock();
            if (linked) {
                poll_page(want, JOLLY_WANT_APPS, "/v1/apps", JOLLY_APPS_MAX, PAGE_MS, &last[0], etags[0]);
                poll_page(want, JOLLY_WANT_EVENTS, "/v1/events?n=10", JOLLY_EVENTS_MAX, PAGE_MS, &last[1], etags[1]);
                poll_page(want, JOLLY_WANT_SERIES_10M, "/v1/series?m=net&w=10m", JOLLY_SERIES_MAX, PAGE_MS, &last[2],
                          etags[2]);
                poll_page(want, JOLLY_WANT_SERIES_24H, "/v1/series?m=net&w=24h", JOLLY_SERIES_MAX, DAY_MS, &last[3],
                          etags[3]);
            }
        }
        /* A new URL or key wakes it early (changed_locked) and starts over. */
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(muse_ui_dark() ? POLL_DARK_MS : POLL_MS))) {
            glance_etag[0] = '\0';
            for (int i = 0; i < 4; i++) {
                etags[i][0] = '\0';
                last[i] = 0;
            }
        }
    }
}

void jolly_client_start(void)
{
    lock();
    load_locked();
    if (s_url[0]) {
        ESP_LOGI(TAG, "Guardián %s, key %u chars", s_url, (unsigned)strlen(s_key));
    }
    unlock();
    if (s_task) {
        return;
    }
    s_body = heap_caps_malloc(BODY_MAX + 1, MUSE_BIG_CAPS);
    /* Stack in PSRAM, as the chat and TTS tasks': it never writes flash (the
     * console does). */
    if (!s_body || xTaskCreatePinnedToCoreWithCaps(poll_task, "jolly_grd", 6144, NULL, 4, &s_task, 0,
                                                   MUSE_BIG_CAPS) != pdPASS) {
        ESP_LOGE(TAG, "start failed: no Guardián");
        s_task = NULL;
    }
}

int32_t jolly_client_alarm_s(void)
{
    lock();
    load_locked();
    int32_t s = s_alarm_s;
    unlock();
    return s;
}

bool jolly_client_snapshot(jolly_glance_t *glance, jolly_linkstate_t *link)
{
    lock();
    jolly_link_tick(&s_link, now_ms());
    bool configured = s_url[0] != '\0';
    *link = s_link;
    if (!configured) {
        link->link = JL_UNREACHABLE;
    }
    bool have = s_have_glance;
    if (have && glance) {
        *glance = s_glance;
    }
    unlock();
    return have;
}

void jolly_client_want(unsigned mask)
{
    lock();
    bool more = mask & ~s_want;
    s_want = mask;
    if (more && s_task) {
        xTaskNotifyGive(s_task);   /* a page just opened: fetch its data now */
    }
    unlock();
}

bool jolly_client_apps(jolly_apps_t *out)
{
    lock();
    bool have = s_have_apps && (s_want & JOLLY_WANT_APPS);
    if (have) {
        *out = s_apps;
    }
    unlock();
    return have;
}

bool jolly_client_events(jolly_events_t *out)
{
    lock();
    bool have = s_have_events && (s_want & JOLLY_WANT_EVENTS);
    if (have) {
        *out = s_events;
    }
    unlock();
    return have;
}

bool jolly_client_series(bool day, jolly_series_t *out)
{
    lock();
    bool have = s_have_series[day];
    if (have) {
        *out = s_series[day];
    }
    unlock();
    return have;
}
