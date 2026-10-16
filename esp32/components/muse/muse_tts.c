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

#include "muse_tts.h"

#include <stdatomic.h>
#include <stdio.h>
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
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "muse_mem.h"

static const char *TAG = "muse_tts";

#define NVS_NS "muse_tts"
#define URL_MAX 160                 /* "http://host:port", and a path if it has one */
#define KEY_MAX 128
#define BODY_MAX 4000               /* the server's limit */
#define RATE 16000
#define RING_BYTES (RATE * 2 * 4)   /* 4 s of speech ahead of the speaker, in PSRAM */
#define CHUNK 2048                  /* bytes per read: 64 ms of speech */
#define CONNECT_TIMEOUT_MS 3000     /* connecting, and the response's headers */
#define STREAM_TIMEOUT_MS 10000     /* a gap in the speech: the server synthesizes per sentence */
#define PUSH_WAIT_MS 50             /* how often a full ring checks for a cancel */

#ifndef CONFIG_MUSE_TTS_URL
#define CONFIG_MUSE_TTS_URL ""
#endif
#ifndef CONFIG_MUSE_TTS_KEY
#define CONFIG_MUSE_TTS_KEY ""
#endif

static portMUX_TYPE s_init_mux = portMUX_INITIALIZER_UNLOCKED;
static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock;    /* the URL and key */
static bool s_loaded;
/* In PSRAM where static data can go: internal RAM is scarce once Wi-Fi is up. */
EXT_RAM_BSS_ATTR static char s_url[URL_MAX];
EXT_RAM_BSS_ATTR static char s_key[KEY_MAX];

static StreamBufferHandle_t s_ring; /* speech: written by the worker, read by the chat task */
static TaskHandle_t s_task;
static char *s_text;                /* BODY_MAX + 1, the request's body */
static size_t s_text_len;
static uint8_t *s_chunk;            /* CHUNK + 1: a read, after a carried odd byte */
EXT_RAM_BSS_ATTR static char s_content_type[48];

static atomic_int s_state = MUSE_TTS_IDLE;
static atomic_bool s_busy;          /* the worker has a request, until it's closed */
static atomic_bool s_cancel;
static atomic_bool s_got_audio;

/* ---- Settings ---- */

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

static void load_str(nvs_handle_t h, const char *key, char *out, size_t cap, const char *dflt)
{
    size_t len = cap;
    if (!h || nvs_get_str(h, key, out, &len) != ESP_OK) {
        strlcpy(out, dflt, cap);
    }
}

/* Under the lock: the saved values, or the Kconfig defaults. */
static void load_locked(void)
{
    if (s_loaded) {
        return;
    }
    nvs_handle_t h = 0;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        h = 0;   /* nothing saved yet */
    }
    load_str(h, "url", s_url, sizeof(s_url), CONFIG_MUSE_TTS_URL);
    load_str(h, "key", s_key, sizeof(s_key), CONFIG_MUSE_TTS_KEY);
    if (h) {
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
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

bool muse_tts_configured(void)
{
    lock();
    load_locked();
    bool set = s_url[0] != '\0';
    unlock();
    return set;
}

/* An http:// URL without spaces or quotes, which go into the status JSON. */
static bool url_ok(const char *v)
{
    if (strncmp(v, "http://", 7) != 0 || strlen(v) >= URL_MAX || !v[7]) {
        return false;
    }
    for (const char *p = v; *p; p++) {
        if (*p <= ' ' || *p == '"' || *p == '\\' || *p == 0x7f) {
            return false;
        }
    }
    return true;
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

bool muse_tts_console_command(const char *line)
{
    if (!strncmp(line, "tts_url=", 8)) {
        const char *v = line + 8;
        if (v[0] && !url_ok(v)) {
            answer("tts_url", "invalid (want http://host:port)");
            return true;
        }
        lock();
        load_locked();
        esp_err_t err = save("url", v);
        if (err == ESP_OK) {
            strlcpy(s_url, v, sizeof(s_url));
        }
        unlock();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "couldn't store the TTS URL: %s", esp_err_to_name(err));
            answer("tts_url", "storage");
            return true;
        }
        ESP_LOGI(TAG, "TTS server %s", v[0] ? v : "cleared: replies are text");
        answer("tts_url", NULL);
        return true;
    }
    if (!strncmp(line, "tts_key=", 8)) {
        const char *v = line + 8;
        size_t len = strlen(v);
        if (len >= KEY_MAX) {
            answer("tts_key", "too long");
            return true;
        }
        lock();
        load_locked();
        esp_err_t err = save("key", v);
        if (err == ESP_OK) {
            strlcpy(s_key, v, sizeof(s_key));
        }
        unlock();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "couldn't store the TTS key: %s", esp_err_to_name(err));
            answer("tts_key", "storage");
            return true;
        }
        ESP_LOGI(TAG, "TTS key %s (%u chars)", len ? "set" : "cleared", (unsigned)len);
        answer("tts_key", NULL);
        return true;
    }
    if (!strcmp(line, "tts")) {
        lock();
        load_locked();
        printf("@tts {\"url\":\"%s\",\"key_len\":%u,\"busy\":%s}\n", s_url, (unsigned)strlen(s_key),
               atomic_load(&s_busy) ? "true" : "false");
        unlock();
        fflush(stdout);
        return true;
    }
    return false;
}

/* ---- Text ---- */

/* Drops an incomplete UTF-8 sequence from the end of out[0..n). */
static size_t utf8_trim(const char *out, size_t n)
{
    size_t i = n;
    while (i > 0 && ((unsigned char)out[i - 1] & 0xC0) == 0x80) {
        i--;
    }
    if (i == 0) {
        return n;
    }
    unsigned char lead = (unsigned char)out[i - 1];
    size_t want = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    return n - (i - 1) < want ? i - 1 : n;
}

/*
 * The request's body: markdown's emphasis, code and heading marks dropped
 * (the server cleans up too), and a text cut short ending at its last whole
 * sentence, or word.
 */
static size_t prepare(const char *in, size_t len, bool cut, char *out)
{
    if (len > BODY_MAX) {
        len = BODY_MAX;
        cut = true;
    }
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        char c = in[i];
        if (c == '*' || c == '`' || c == '#') {
            continue;
        }
        out[o++] = c;
    }
    o = utf8_trim(out, o);
    if (cut) {
        size_t end = 0;
        for (size_t i = 0; i < o; i++) {
            if (out[i] == '.' || out[i] == '!' || out[i] == '?' || out[i] == '\n') {
                end = i + 1;
            }
        }
        if (end && end >= o / 3) {
            o = end;
        } else {
            while (o && out[o - 1] != ' ') {
                o--;
            }
        }
    }
    while (o && (out[o - 1] == ' ' || out[o - 1] == '\n' || out[o - 1] == '\r' || out[o - 1] == '\t')) {
        o--;
    }
    out[o] = '\0';
    return o;
}

/* ---- The request ---- */

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_HEADER && evt->header_key && evt->header_value &&
        !strcasecmp(evt->header_key, "Content-Type")) {
        strlcpy(s_content_type, evt->header_value, sizeof(s_content_type));
    }
    return ESP_OK;
}

/* Hands speech to the ring, waiting while it's full. False if cancelled. */
static bool push(const uint8_t *data, size_t len)
{
    while (len) {
        if (atomic_load(&s_cancel)) {
            return false;
        }
        size_t sent = xStreamBufferSend(s_ring, data, len, pdMS_TO_TICKS(PUSH_WAIT_MS));
        data += sent;
        len -= sent;
    }
    return true;
}

/* Reads the speech into the ring until it ends. */
static bool stream_speech(esp_http_client_handle_t c, int64_t t0)
{
    size_t carry = 0, total = 0;
    for (;;) {
        if (atomic_load(&s_cancel)) {
            ESP_LOGI(TAG, "speech stopped after %.1f s", total / 2.0 / RATE);
            return false;
        }
        int n = esp_http_client_read(c, (char *)s_chunk + carry, CHUNK);
        if (n == -ESP_ERR_HTTP_EAGAIN) {
            ESP_LOGW(TAG, "speech stalled for %d s, giving up", STREAM_TIMEOUT_MS / 1000);
            return false;
        }
        if (n < 0) {
            ESP_LOGW(TAG, "speech stream broke after %.1f s", total / 2.0 / RATE);
            return false;
        }
        if (n == 0) {
            if (!esp_http_client_is_complete_data_received(c)) {
                ESP_LOGW(TAG, "speech stream ended early, after %.1f s", total / 2.0 / RATE);
                return false;
            }
            ESP_LOGI(TAG, "speech done: %.1f s in %d ms", total / 2.0 / RATE,
                     (int)((esp_timer_get_time() - t0) / 1000));
            return true;
        }
        size_t have = carry + (size_t)n, even = have & ~(size_t)1;
        if (!total && even) {
            ESP_LOGI(TAG, "first speech after %d ms", (int)((esp_timer_get_time() - t0) / 1000));
        }
        if (even) {
            atomic_store(&s_got_audio, true);
            if (!push(s_chunk, even)) {
                ESP_LOGI(TAG, "speech stopped after %.1f s", (total + even) / 2.0 / RATE);
                return false;
            }
            total += even;
        }
        carry = have - even;   /* half a sample waits for the next read */
        if (carry) {
            s_chunk[0] = s_chunk[even];
        }
    }
}

static bool fetch(void)
{
    if (atomic_load(&s_cancel)) {
        return false;   /* cancelled before it started */
    }
    char url[URL_MAX + 8];
    char key[KEY_MAX];
    lock();
    strlcpy(url, s_url, sizeof(url));
    strlcpy(key, s_key, sizeof(key));
    unlock();
    size_t ul = strlen(url);
    while (ul > 7 && url[ul - 1] == '/') {
        url[--ul] = '\0';
    }
    if (ul < 4 || strcmp(url + ul - 4, "/tts") != 0) {
        strlcat(url, "/tts", sizeof(url));
    }

    int64_t t0 = esp_timer_get_time();
    s_content_type[0] = '\0';
    const esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = CONNECT_TIMEOUT_MS,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
        .disable_auto_redirect = true,
        .event_handler = on_http_event,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        ESP_LOGW(TAG, "no memory for the request");
        return false;
    }
    bool ok = false;
    esp_http_client_set_header(c, "Content-Type", "text/plain; charset=utf-8");
    if (key[0]) {
        esp_http_client_set_header(c, "X-Muse-Voz-Key", key);
    }
    memset(key, 0, sizeof(key));
    esp_err_t err = esp_http_client_open(c, (int)s_text_len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "can't reach %s: %s", url, esp_err_to_name(err));
        goto done;
    }
    if (atomic_load(&s_cancel)) {
        goto done;
    }
    if (esp_http_client_write(c, s_text, (int)s_text_len) != (int)s_text_len) {
        ESP_LOGW(TAG, "couldn't send the text to %s", url);
        goto done;
    }
    if (esp_http_client_fetch_headers(c) < 0) {
        ESP_LOGW(TAG, "no answer from %s", url);
        goto done;
    }
    int status = esp_http_client_get_status_code(c);
    if (status != 200) {
        char body[128];
        int n = esp_http_client_read(c, body, sizeof(body) - 1);
        body[n > 0 ? n : 0] = '\0';
        ESP_LOGW(TAG, "%s answered %d: %s", url, status, body);
        goto done;
    }
    if (strncasecmp(s_content_type, "audio/L16", 9) != 0) {
        ESP_LOGW(TAG, "%s sent %s, not raw 16 kHz speech", url, s_content_type[0] ? s_content_type : "no type");
        goto done;
    }
    esp_http_client_set_timeout_ms(c, STREAM_TIMEOUT_MS);
    ok = stream_speech(c, t0);
done:
    esp_http_client_cleanup(c);
    return ok;
}

static void tts_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        bool ok = fetch() && !atomic_load(&s_cancel);
        /* Free before the state says so: the chat task starts the next message
         * as soon as it sees this one end. */
        atomic_store(&s_busy, false);
        atomic_store(&s_state, ok ? MUSE_TTS_ENDED : MUSE_TTS_FAILED);
    }
}

/* ---- API ---- */

void muse_tts_init(void)
{
    lock();
    load_locked();
    if (s_url[0]) {
        ESP_LOGI(TAG, "TTS server %s, key %u chars", s_url, (unsigned)strlen(s_key));
    }
    unlock();
    if (s_task) {
        return;
    }
    s_ring = xStreamBufferCreateWithCaps(RING_BYTES, 2, MUSE_BIG_CAPS);
    s_text = heap_caps_malloc(BODY_MAX + 1, MUSE_BIG_CAPS);
    s_chunk = heap_caps_malloc(CHUNK + 1, MUSE_BIG_CAPS);
    /* Stack in PSRAM, as the chat task's: lwIP sockets don't need internal RAM. */
    if (!s_ring || !s_text || !s_chunk ||
        xTaskCreatePinnedToCoreWithCaps(tts_task, "muse_tts", 6144, NULL, 5, &s_task, 0, MUSE_BIG_CAPS) != pdPASS) {
        ESP_LOGE(TAG, "start failed: replies stay text");
        s_task = NULL;
    }
}

bool muse_tts_begin(const char *text, size_t len, bool cut)
{
    if (!s_task || !text || !muse_tts_configured()) {
        return false;
    }
    if (atomic_load(&s_busy)) {
        ESP_LOGW(TAG, "the last request is still closing; this one stays text");
        return false;
    }
    s_text_len = prepare(text, len, cut, s_text);
    if (!s_text_len) {
        return false;
    }
    /* The worker is idle and the caller is the only reader: nothing waits on the ring. */
    if (xStreamBufferReset(s_ring) != pdPASS) {
        return false;
    }
    atomic_store(&s_cancel, false);
    atomic_store(&s_got_audio, false);
    atomic_store(&s_state, MUSE_TTS_RUNNING);
    atomic_store(&s_busy, true);
    xTaskNotifyGive(s_task);
    return true;
}

muse_tts_state_t muse_tts_state(bool *got_audio)
{
    if (got_audio) {
        *got_audio = atomic_load(&s_got_audio);
    }
    return (muse_tts_state_t)atomic_load(&s_state);
}

size_t muse_tts_read(int16_t *pcm, size_t frames)
{
    if (!s_ring) {
        return 0;
    }
    return xStreamBufferReceive(s_ring, pcm, frames * sizeof(int16_t), 0) / sizeof(int16_t);
}

size_t muse_tts_pending(void)
{
    return s_ring ? xStreamBufferBytesAvailable(s_ring) / sizeof(int16_t) : 0;
}

void muse_tts_cancel(void)
{
    if (atomic_load(&s_busy)) {
        atomic_store(&s_cancel, true);
    } else {
        atomic_store(&s_state, MUSE_TTS_IDLE);
    }
}
