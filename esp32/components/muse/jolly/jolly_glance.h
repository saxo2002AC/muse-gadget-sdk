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

#include "jolly/jolly_mood.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The Guardián's contract v1 (the guardian plan, §1): what the board reads
 * from it, parsed strictly (Diana's R15): the body under its endpoint's cap,
 * "v" 1, every field of the right type, enums known, integers clamped to
 * their range, strings cut to their buffer on a UTF-8 boundary with control
 * characters dropped, arrays taken up to the contract's length. Anything else
 * and the parse fails, which counts as a failed poll. Plus the ETag check
 * and the link state (§1.8). Pure: tests/jolly/test_glance.c.
 */

#define JOLLY_NONE INT32_MIN       /* a nullable field that came as null */

/* Body caps per endpoint (R15). */
#define JOLLY_GLANCE_MAX 1536
#define JOLLY_APPS_MAX 3072
#define JOLLY_EVENTS_MAX 4096
#define JOLLY_SERIES_MAX 2048

#define JOLLY_HEAD_MAX 48          /* bytes */
#define JOLLY_ID_MAX 24
#define JOLLY_TXT_MAX 64
#define JOLLY_DOWN_MAX 3
#define JOLLY_APPS_N 32
#define JOLLY_EVENTS_N 20

typedef enum { JS_OK, JS_WARN, JS_CRIT, JS_UNKNOWN } jolly_state_t;   /* backup, monitor */
typedef enum { JT_NORMAL, JT_ALTO, JT_CRITICO } jolly_therm_t;
typedef enum { JA_UP, JA_DOWN, JA_DEP, JA_UNK } jolly_app_st_t;

typedef struct {
    char id[JOLLY_ID_MAX + 1];
    int32_t port, for_s;
} jolly_app_down_t;

typedef struct {
    int64_t ts;
    int32_t seq, age_s;
    jolly_level_t level;
    bool quiet, silenced;
    int32_t crit, warn;
    char head[JOLLY_HEAD_MAX + 1];
    struct {
        int32_t up_s, cpu, temp;   /* temp: JOLLY_NONE when not measured */
        jolly_therm_t therm;
        jolly_level_t mem;
        int32_t swap_mb, swap_tot_mb, disk_pct, disk_free_gb;
        char macos[16];
        int32_t updates;           /* JOLLY_NONE when not known */
        jolly_state_t backup_state, monitor_state;
        int32_t backup_age_s, monitor_age_s;   /* JOLLY_NONE when not known */
    } mac;
    struct {
        int32_t up, total;
        int ndown;
        jolly_app_down_t down[JOLLY_DOWN_MAX];
    } apps;
    struct {
        int32_t lat_ms;            /* JOLLY_NONE when not measured */
        int32_t rx_kbps, tx_kbps, cf, cf_exp;
        char ts_state[12];
        int32_t ts_online;
        bool has_speed;
        int32_t speed_down, speed_up, speed_age_s;
    } net;
    struct {
        jolly_level_t level;
        int32_t ports, ports_unreg, plists, ts_nodes, ssh_fail_24h;
        bool learning;
    } sec;
} jolly_glance_t;

typedef struct {
    int64_t ts;
    int32_t seq;
    int n;
    struct {
        char id[JOLLY_ID_MAX + 1];
        int32_t port, for_s;
        jolly_app_st_t st;
    } apps[JOLLY_APPS_N];
} jolly_apps_t;

typedef struct {
    int64_t ts;
    int32_t seq;
    int n;
    struct {
        int64_t t;
        jolly_level_t sev;         /* info comes as JLV_OK */
        char cat[12];
        char txt[JOLLY_TXT_MAX + 1];
        bool open;
    } ev[JOLLY_EVENTS_N];
} jolly_events_t;

typedef struct {
    int64_t ts, t0;
    int32_t step_s;
    int n;                         /* 60 for 10m, 24 for 24h */
    int32_t rx[60], tx[60];        /* -1 is a gap */
    bool has_tx;                   /* 10m only */
    int32_t rx_peak_kbps;          /* 24h only */
} jolly_series_t;

bool jolly_glance_parse(const char *json, size_t len, jolly_glance_t *out);
bool jolly_apps_parse(const char *json, size_t len, jolly_apps_t *out);
bool jolly_events_parse(const char *json, size_t len, jolly_events_t *out);
bool jolly_series_parse(const char *json, size_t len, jolly_series_t *out);

/* Any change of seq is new: it restarts at 1 when the Guardián restarts. */
bool jolly_glance_is_new(int32_t old_seq, int32_t new_seq);

/* An ETag fit to send back in If-None-Match: 1 to 32 bytes of printable
 * ASCII, so no CR/LF can inject a header (R15). */
bool jolly_etag_ok(const char *etag);

/* The link to the Guardián (§1.8). */
typedef struct {
    jolly_link_t link;
    int fails;                     /* consecutive failed polls */
    int64_t last_ok_ms;            /* the last 200 or 304 */
    bool have_ok;
    int auth_status;               /* 401 or 403 while JL_UNAUTH */
} jolly_linkstate_t;

void jolly_link_init(jolly_linkstate_t *s);

/* A poll's result: its HTTP status (0 for no answer, or a body that didn't
 * parse), whether Wi-Fi is up, and the glance's age_s on a 200 or 304. */
void jolly_link_on_result(jolly_linkstate_t *s, int http_status, bool wifi_up, int age_s, int64_t now_ms);

/* Between polls: more than 15 s since the last answer is unreachable. */
void jolly_link_tick(jolly_linkstate_t *s, int64_t now_ms);

#ifdef __cplusplus
}
#endif
