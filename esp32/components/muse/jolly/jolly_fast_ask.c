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

#include "jolly/jolly_fast_ask.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "jolly/jolly_clock.h"
#include "jolly/jolly_fast.h"
#include "jolly/jolly_fast_auth.h"
#include "muse_mem.h"
#include "muse_resolve.h"
#include "muse_url.h"

static const char *TAG = "jolly_fast";

#define RATE 16000
#define MIN_FRAMES (RATE * 3 / 10)   /* the contract's 9600 bytes */
#define WRITE_CHUNK 4096

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static jolly_fast_state_t s_state = JFA_IDLE;
static jolly_fast_answer_t s_answer;   /* JFA_LOCAL's */
static uint32_t s_gen;                 /* the ask that counts */
static int64_t s_deadline_ms;
static bool s_running;                 /* a worker is alive */
static jolly_fast_breaker_t s_breaker;

typedef struct {
    uint32_t gen;
    int16_t *pcm;
    size_t bytes;
    jolly_fast_cfg_t cfg;
    int64_t t0_ms, deadline_ms;
} job_t;

typedef struct {
    char content_type[48];
    char sig[72];
} heads_t;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

const char *jolly_fast_breaker_name(void)
{
    taskENTER_CRITICAL(&s_mux);
    bool closed = jolly_fast_breaker_closed(&s_breaker, now_ms());
    taskEXIT_CRITICAL(&s_mux);
    return closed ? "closed" : "open";
}

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    heads_t *h = evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_HEADER || !evt->header_key || !evt->header_value || !h) {
        return ESP_OK;
    }
    if (!strcasecmp(evt->header_key, "Content-Type")) {
        strlcpy(h->content_type, evt->header_value, sizeof(h->content_type));
    } else if (!strcasecmp(evt->header_key, "X-Jolly-Sig")) {
        strlcpy(h->sig, evt->header_value, sizeof(h->sig));
    }
    return ESP_OK;
}

/* The request; *why says what went wrong. Fills *a on a valid answer. */
static bool ask(const job_t *j, jolly_fast_answer_t *a, const char **why)
{
    muse_url_t u;
    char ip[16];
    if (!muse_url_parse(j->cfg.url, &u) || !muse_resolve(&u, ip)) {
        *why = "resolve";
        return false;
    }
    int64_t left = j->deadline_ms - now_ms();
    if (left < 50) {
        *why = "slow";
        return false;
    }
    uint8_t sha[32], rnd[16];
    jolly_sha256(j->pcm, j->bytes, sha);
    esp_fill_random(rnd, sizeof(rnd));
    char nonce[33], auth[65], ts[24], len[12];
    jolly_hex(rnd, sizeof(rnd), nonce);
    long long t = (long long)time(NULL);
    snprintf(ts, sizeof(ts), "%lld", t);
    snprintf(len, sizeof(len), "%u", (unsigned)j->bytes);
    jolly_fast_auth(j->cfg.key, t, nonce, sha, auth);

    char url[64];
    snprintf(url, sizeof(url), "http://%s:%d/v1/ask", ip, u.port);
    heads_t heads = { "", "" };
    const esp_http_client_config_t hc = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = (int)left,   /* each socket wait; the deadline bounds the whole */
        .disable_auto_redirect = true,
        .keep_alive_enable = false,
        .buffer_size = 1100,
        .buffer_size_tx = 512,
        .event_handler = on_http_event,
        .user_data = &heads,
    };
    esp_http_client_handle_t c = esp_http_client_init(&hc);
    if (!c) {
        *why = "memory";
        return false;
    }
    esp_http_client_set_header(c, "Content-Type", "audio/L16; rate=16000; channels=1");
    esp_http_client_set_header(c, "X-Jolly-Ts", ts);
    esp_http_client_set_header(c, "X-Jolly-Nonce", nonce);
    esp_http_client_set_header(c, "X-Jolly-Auth", auth);
    esp_http_client_set_header(c, "Connection", "close");
    bool ok = false;
    char *body = NULL;
    if (esp_http_client_open(c, (int)j->bytes) != ESP_OK) {
        *why = "connect";
        muse_resolve_forget(&u);   /* a .local Mini may have a new address */
        goto done;
    }
    for (size_t off = 0; off < j->bytes;) {
        if (now_ms() > j->deadline_ms) {
            *why = "slow";
            goto done;
        }
        size_t n = j->bytes - off < WRITE_CHUNK ? j->bytes - off : WRITE_CHUNK;
        int w = esp_http_client_write(c, (const char *)j->pcm + off, (int)n);
        if (w <= 0) {
            *why = "send";
            goto done;
        }
        off += (size_t)w;
    }
    int64_t clen = esp_http_client_fetch_headers(c);
    int status = esp_http_client_get_status_code(c);
    if (status != 200) {
        *why = status ? "status" : "no answer";
        goto done;
    }
    if (strncasecmp(heads.content_type, "application/json", 16) != 0 ||
        (heads.content_type[16] && heads.content_type[16] != ';' && heads.content_type[16] != ' ')) {
        *why = "type";
        goto done;
    }
    if (clen <= 0 || clen > JOLLY_FAST_BODY_MAX) {
        *why = "size";
        goto done;
    }
    body = heap_caps_malloc(JOLLY_FAST_BODY_MAX + 2, MUSE_BIG_CAPS);
    if (!body) {
        *why = "memory";
        goto done;
    }
    int got = 0;
    while (got < clen) {
        int r = esp_http_client_read(c, body + got, JOLLY_FAST_BODY_MAX + 1 - got);
        if (r <= 0) {
            break;
        }
        got += r;
    }
    if (got != clen) {
        *why = "short body";
        goto done;
    }
    if (now_ms() > j->deadline_ms) {
        *why = "slow";
        goto done;
    }
    /* Signed by the key we never send (Diana C15): nobody else can make Jolly talk. */
    if (!jolly_fast_sig_ok(j->cfg.key, nonce, body, (size_t)got, heads.sig)) {
        *why = "signature";
        goto done;
    }
    if (!jolly_fast_parse(body, (size_t)got, a)) {
        *why = "contract";
        goto done;
    }
    ok = true;
done:
    if (body) {
        heap_caps_free(body);
    }
    esp_http_client_cleanup(c);
    return ok;
}

static void worker(void *arg)
{
    job_t *j = arg;
    jolly_fast_answer_t a;
    const char *why = "";
    bool ok = ask(j, &a, &why);
    int64_t took = now_ms() - j->t0_ms;
    bool late = now_ms() > j->deadline_ms;
    if (late && ok) {
        ok = false;
        why = "slow";
    }

    taskENTER_CRITICAL(&s_mux);
    bool current = j->gen == s_gen;   /* not cancelled, not superseded */
    if (current) {
        if (ok && a.local) {
            s_answer = a;
        }
        s_state = ok ? (a.local ? JFA_LOCAL : JFA_FORWARD) : JFA_ERROR;
        jolly_fast_breaker_result(&s_breaker, ok, now_ms());
    }
    s_running = false;
    taskEXIT_CRITICAL(&s_mux);

    char last[48];
    if (ok) {
        snprintf(last, sizeof(last), "%s %s %dms", a.local ? "local" : "forward", a.local ? a.intent : a.reason,
                 (int)took);
        ESP_LOGI(TAG, "fast: %s %s=%s ask=%dms stt=%dms%s", a.local ? "local" : "forward", a.local ? "intent" : "reason",
                 a.local ? a.intent : a.reason, (int)took, (int)a.stt_ms, current ? "" : " (no longer wanted)");
        if (j->cfg.log) {   /* >fast_log=1 only (C13): what it heard and said */
            ESP_LOGI(TAG, "fast: heard \"%s\" said \"%s\"", a.transcript, a.text);
        }
    } else {
        snprintf(last, sizeof(last), "error %s %dms", why, (int)took);
        ESP_LOGW(TAG, "fast: error %s ask=%dms%s", why, (int)took, current ? "" : " (no longer wanted)");
    }
    jolly_fast_note_last(last);
    jolly_wipe(&a, sizeof(a));
    jolly_wipe(j->cfg.key, sizeof(j->cfg.key));
    heap_caps_free(j->pcm);
    heap_caps_free(j);
    vTaskDeleteWithCaps(NULL);   /* created WithCaps: its stack and TCB go back to PSRAM */
}

bool jolly_fast_ask_start(const int16_t *pcm, size_t frames)
{
    jolly_fast_cfg_t cfg;
    if (!jolly_fast_config(&cfg)) {
        return false;   /* off, or not set up */
    }
    const char *skip = NULL;
    taskENTER_CRITICAL(&s_mux);
    if (s_running) {
        skip = "busy";
    } else if (!jolly_fast_breaker_closed(&s_breaker, now_ms())) {
        skip = "breaker open";
    }
    taskEXIT_CRITICAL(&s_mux);
    if (!skip && !jolly_clock_synced()) {
        skip = "no clock";
    } else if (!skip && (frames < MIN_FRAMES || frames > (size_t)cfg.max_s * RATE)) {
        skip = "length";
    }
    job_t *j = skip ? NULL : heap_caps_calloc(1, sizeof(job_t), MUSE_BIG_CAPS);
    if (j) {
        j->bytes = frames * sizeof(int16_t);
        j->pcm = heap_caps_malloc(j->bytes, MUSE_BIG_CAPS);
        if (!j->pcm) {
            heap_caps_free(j);
            j = NULL;
            skip = "memory";
        }
    } else if (!skip) {
        skip = "memory";
    }
    if (skip) {
        jolly_wipe(cfg.key, sizeof(cfg.key));
        ESP_LOGI(TAG, "fast: not asked (%s)", skip);
        return false;
    }
    memcpy(j->pcm, pcm, j->bytes);
    j->cfg = cfg;
    jolly_wipe(cfg.key, sizeof(cfg.key));
    j->t0_ms = now_ms();
    j->deadline_ms = j->t0_ms + cfg.ms;
    taskENTER_CRITICAL(&s_mux);
    j->gen = ++s_gen;
    s_state = JFA_PENDING;
    s_deadline_ms = j->deadline_ms;
    s_running = true;
    taskEXIT_CRITICAL(&s_mux);
    if (xTaskCreatePinnedToCoreWithCaps(worker, "jolly_fast", 6144, j, 4, NULL, tskNO_AFFINITY, MUSE_BIG_CAPS) !=
        pdPASS) {
        taskENTER_CRITICAL(&s_mux);
        s_running = false;
        s_state = JFA_ERROR;
        taskEXIT_CRITICAL(&s_mux);
        jolly_wipe(j->cfg.key, sizeof(j->cfg.key));
        heap_caps_free(j->pcm);
        heap_caps_free(j);
        ESP_LOGW(TAG, "fast: no task");
        return false;
    }
    return true;
}

jolly_fast_state_t jolly_fast_ask_poll(jolly_fast_answer_t *out)
{
    taskENTER_CRITICAL(&s_mux);
    if (s_state == JFA_PENDING && now_ms() > s_deadline_ms) {
        s_state = JFA_ERROR;   /* past the deadline: Muse's, whatever comes (the worker counts it) */
    }
    jolly_fast_state_t st = s_state;
    if (st == JFA_LOCAL && out) {
        *out = s_answer;
    }
    taskEXIT_CRITICAL(&s_mux);
    return st;
}

void jolly_fast_ask_cancel(void)
{
    taskENTER_CRITICAL(&s_mux);
    s_gen++;   /* the running worker's answer won't count, nor feed the breaker */
    s_state = JFA_IDLE;
    memset(&s_answer, 0, sizeof(s_answer));
    taskEXIT_CRITICAL(&s_mux);
}
