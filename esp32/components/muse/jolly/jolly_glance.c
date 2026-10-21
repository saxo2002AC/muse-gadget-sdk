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

#include "jolly/jolly_glance.h"

#include <string.h>

#include "cJSON.h"

#define LINK_STALE_S 60            /* age_s past this: no data (§1.8) */
#define LINK_TIMEOUT_MS 15000      /* no answer this long: unreachable */
#define LINK_FAILS 3

/* ---- Field readers: false for a missing key or a wrong type ---- */

static const cJSON *field(const cJSON *o, const char *key)
{
    return cJSON_IsObject(o) ? cJSON_GetObjectItemCaseSensitive(o, key) : NULL;
}

static bool get_i64(const cJSON *o, const char *key, int64_t lo, int64_t hi, int64_t *out)
{
    const cJSON *it = field(o, key);
    if (!cJSON_IsNumber(it)) {
        return false;
    }
    double v = it->valuedouble;
    *out = v < (double)lo ? lo : v > (double)hi ? hi : (int64_t)v;
    return true;
}

/* An integer clamped to lo..hi; with `nullable`, null reads as JOLLY_NONE. */
static bool get_int(const cJSON *o, const char *key, int32_t lo, int32_t hi, bool nullable, int32_t *out)
{
    const cJSON *it = field(o, key);
    if (nullable && cJSON_IsNull(it)) {
        *out = JOLLY_NONE;
        return true;
    }
    int64_t v;
    if (!get_i64(o, key, lo, hi, &v)) {
        return false;
    }
    *out = (int32_t)v;
    return true;
}

static bool get_bool(const cJSON *o, const char *key, bool *out)
{
    const cJSON *it = field(o, key);
    if (!cJSON_IsBool(it)) {
        return false;
    }
    *out = cJSON_IsTrue(it);
    return true;
}

/* A string into buf: control characters dropped, cut on a UTF-8 boundary. */
static bool get_str(const cJSON *o, const char *key, char *buf, size_t cap)
{
    const cJSON *it = field(o, key);
    if (!cJSON_IsString(it) || !it->valuestring) {
        return false;
    }
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)it->valuestring; *p && n < cap - 1; p++) {
        if (*p >= 0x20 && *p != 0x7f) {
            buf[n++] = (char)*p;
        }
    }
    /* Don't end on half a character: drop an incomplete trailing sequence. */
    size_t i = n;
    while (i > 0 && ((unsigned char)buf[i - 1] & 0xC0) == 0x80) {
        i--;
    }
    if (i > 0) {
        unsigned char lead = (unsigned char)buf[i - 1];
        size_t want = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        if (n - (i - 1) < want) {
            n = i - 1;
        }
    }
    buf[n] = '\0';
    return true;
}

static bool get_enum(const cJSON *o, const char *key, const char *const *names, int count, int *out)
{
    const cJSON *it = field(o, key);
    if (!cJSON_IsString(it) || !it->valuestring) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (!strcmp(it->valuestring, names[i])) {
            *out = i;
            return true;
        }
    }
    return false;
}

static const char *const LEVELS[] = { "ok", "warn", "crit" };
static const char *const STATES[] = { "ok", "warn", "crit", "unknown" };
static const char *const THERMS[] = { "normal", "alto", "critico" };
static const char *const APP_STS[] = { "up", "down", "dep", "unk" };
static const char *const SEVS[] = { "info", "warn", "crit" };

static bool get_level(const cJSON *o, const char *key, jolly_level_t *out)
{
    int v;
    if (!get_enum(o, key, LEVELS, 3, &v)) {
        return false;
    }
    *out = (jolly_level_t)v;
    return true;
}

static bool get_state(const cJSON *o, const char *key, jolly_state_t *out)
{
    int v;
    if (!get_enum(o, key, STATES, 4, &v)) {
        return false;
    }
    *out = (jolly_state_t)v;
    return true;
}

/* The document, if it's an object of version 1 under `cap` bytes. */
static cJSON *open_doc(const char *json, size_t len, size_t cap)
{
    if (!json || !len || len > cap) {
        return NULL;
    }
    cJSON *root = cJSON_ParseWithLength(json, len);
    int64_t v;
    if (!cJSON_IsObject(root) || !get_i64(root, "v", 0, 1000, &v) || v != 1) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

#define I32 INT32_MAX

/* ---- glance ---- */

static bool parse_mac(const cJSON *m, jolly_glance_t *g)
{
    int therm;
    const cJSON *backup = field(m, "backup"), *monitor = field(m, "monitor");
    return cJSON_IsObject(m) && get_int(m, "up_s", 0, I32, false, &g->mac.up_s) &&
           get_int(m, "cpu", 0, 100, false, &g->mac.cpu) && get_int(m, "temp", -50, 150, true, &g->mac.temp) &&
           get_enum(m, "therm", THERMS, 3, &therm) && (g->mac.therm = (jolly_therm_t)therm, true) &&
           get_level(m, "mem", &g->mac.mem) && get_int(m, "swap_mb", 0, I32, false, &g->mac.swap_mb) &&
           get_int(m, "swap_tot_mb", 0, I32, false, &g->mac.swap_tot_mb) &&
           get_int(m, "disk_pct", 0, 100, false, &g->mac.disk_pct) &&
           get_int(m, "disk_free_gb", 0, I32, false, &g->mac.disk_free_gb) &&
           get_str(m, "macos", g->mac.macos, sizeof(g->mac.macos)) &&
           get_int(m, "updates", 0, 9999, true, &g->mac.updates) && cJSON_IsObject(backup) &&
           get_state(backup, "state", &g->mac.backup_state) &&
           get_int(backup, "age_s", 0, I32, true, &g->mac.backup_age_s) && cJSON_IsObject(monitor) &&
           get_state(monitor, "state", &g->mac.monitor_state) &&
           get_int(monitor, "age_s", 0, I32, true, &g->mac.monitor_age_s);
}

static bool parse_apps_summary(const cJSON *a, jolly_glance_t *g)
{
    const cJSON *down = field(a, "down");
    if (!cJSON_IsObject(a) || !get_int(a, "up", 0, 9999, false, &g->apps.up) ||
        !get_int(a, "total", 0, 9999, false, &g->apps.total) || !cJSON_IsArray(down)) {
        return false;
    }
    g->apps.ndown = 0;
    const cJSON *d;
    cJSON_ArrayForEach(d, down) {
        if (g->apps.ndown == JOLLY_DOWN_MAX) {
            break;   /* the contract's limit; the rest is ignored */
        }
        jolly_app_down_t *x = &g->apps.down[g->apps.ndown];
        if (!get_str(d, "id", x->id, sizeof(x->id)) || !get_int(d, "port", 0, 65535, false, &x->port) ||
            !get_int(d, "for_s", 0, I32, false, &x->for_s)) {
            return false;
        }
        g->apps.ndown++;
    }
    return true;
}

static bool parse_net(const cJSON *n, jolly_glance_t *g)
{
    const cJSON *speed = field(n, "speed");
    if (!cJSON_IsObject(n) || !get_int(n, "lat_ms", 0, 600000, true, &g->net.lat_ms) ||
        !get_int(n, "rx_kbps", 0, I32, false, &g->net.rx_kbps) ||
        !get_int(n, "tx_kbps", 0, I32, false, &g->net.tx_kbps) || !get_int(n, "cf", 0, 99, false, &g->net.cf) ||
        !get_int(n, "cf_exp", 0, 99, false, &g->net.cf_exp) ||
        !get_str(n, "ts_state", g->net.ts_state, sizeof(g->net.ts_state)) ||
        !get_int(n, "ts_online", 0, 9999, false, &g->net.ts_online)) {
        return false;
    }
    g->net.has_speed = false;
    if (cJSON_IsNull(speed)) {
        return true;
    }
    g->net.has_speed = true;
    return cJSON_IsObject(speed) && get_int(speed, "down", 0, I32, false, &g->net.speed_down) &&
           get_int(speed, "up", 0, I32, false, &g->net.speed_up) &&
           get_int(speed, "age_s", 0, I32, false, &g->net.speed_age_s);
}

static bool parse_sec(const cJSON *s, jolly_glance_t *g)
{
    return cJSON_IsObject(s) && get_level(s, "level", &g->sec.level) &&
           get_int(s, "ports", 0, 65535, false, &g->sec.ports) &&
           get_int(s, "ports_unreg", 0, 65535, false, &g->sec.ports_unreg) &&
           get_int(s, "plists", 0, 99999, false, &g->sec.plists) &&
           get_int(s, "ts_nodes", 0, 9999, false, &g->sec.ts_nodes) &&
           get_int(s, "ssh_fail_24h", 0, I32, false, &g->sec.ssh_fail_24h) &&
           get_bool(s, "learning", &g->sec.learning);
}

bool jolly_glance_parse(const char *json, size_t len, jolly_glance_t *out)
{
    cJSON *root = open_doc(json, len, JOLLY_GLANCE_MAX);
    if (!root) {
        return false;
    }
    jolly_glance_t g;
    memset(&g, 0, sizeof(g));
    bool ok = get_i64(root, "ts", 0, INT64_MAX / 2, &g.ts) && get_int(root, "seq", 0, I32, false, &g.seq) &&
              get_int(root, "age_s", 0, I32, false, &g.age_s) && get_level(root, "level", &g.level) &&
              get_bool(root, "quiet", &g.quiet) && get_bool(root, "silenced", &g.silenced) &&
              get_int(root, "crit", 0, 9999, false, &g.crit) && get_int(root, "warn", 0, 9999, false, &g.warn) &&
              get_str(root, "head", g.head, sizeof(g.head)) && parse_mac(field(root, "mac"), &g) &&
              parse_apps_summary(field(root, "apps"), &g) && parse_net(field(root, "net"), &g) &&
              parse_sec(field(root, "sec"), &g);
    cJSON_Delete(root);
    if (ok) {
        *out = g;
    }
    return ok;
}

/* ---- apps, events, series ---- */

bool jolly_apps_parse(const char *json, size_t len, jolly_apps_t *out)
{
    cJSON *root = open_doc(json, len, JOLLY_APPS_MAX);
    if (!root) {
        return false;
    }
    jolly_apps_t a;
    memset(&a, 0, sizeof(a));
    const cJSON *list = field(root, "apps"), *it;
    bool ok = get_i64(root, "ts", 0, INT64_MAX / 2, &a.ts) && get_int(root, "seq", 0, I32, false, &a.seq) &&
              cJSON_IsArray(list);
    const cJSON *walk = ok ? list : NULL;   /* the macro takes a plain expression */
    cJSON_ArrayForEach(it, walk) {
        if (a.n == JOLLY_APPS_N) {
            break;
        }
        int st;
        if (!get_str(it, "id", a.apps[a.n].id, sizeof(a.apps[a.n].id)) ||
            !get_int(it, "port", 0, 65535, false, &a.apps[a.n].port) || !get_enum(it, "st", APP_STS, 4, &st) ||
            !get_int(it, "for_s", 0, I32, false, &a.apps[a.n].for_s)) {
            ok = false;
            break;
        }
        a.apps[a.n++].st = (jolly_app_st_t)st;
    }
    cJSON_Delete(root);
    if (ok) {
        *out = a;
    }
    return ok;
}

bool jolly_events_parse(const char *json, size_t len, jolly_events_t *out)
{
    cJSON *root = open_doc(json, len, JOLLY_EVENTS_MAX);
    if (!root) {
        return false;
    }
    jolly_events_t e;
    memset(&e, 0, sizeof(e));
    const cJSON *list = field(root, "events"), *it;
    bool ok = get_i64(root, "ts", 0, INT64_MAX / 2, &e.ts) && get_int(root, "seq", 0, I32, false, &e.seq) &&
              cJSON_IsArray(list);
    const cJSON *walk = ok ? list : NULL;   /* the macro takes a plain expression */
    cJSON_ArrayForEach(it, walk) {
        if (e.n == JOLLY_EVENTS_N) {
            break;
        }
        int sev;
        if (!get_i64(it, "t", 0, INT64_MAX / 2, &e.ev[e.n].t) || !get_enum(it, "sev", SEVS, 3, &sev) ||
            !get_str(it, "cat", e.ev[e.n].cat, sizeof(e.ev[e.n].cat)) ||
            !get_str(it, "txt", e.ev[e.n].txt, sizeof(e.ev[e.n].txt)) || !get_bool(it, "open", &e.ev[e.n].open)) {
            ok = false;
            break;
        }
        e.ev[e.n++].sev = (jolly_level_t)sev;   /* info, warn, crit -> JLV_OK, JLV_WARN, JLV_CRIT */
    }
    cJSON_Delete(root);
    if (ok) {
        *out = e;
    }
    return ok;
}

/* Exactly n integers, each -1 (a gap) or more. */
static bool get_ints(const cJSON *o, const char *key, int32_t *out, int n)
{
    const cJSON *arr = field(o, key), *it;
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) != n) {
        return false;
    }
    int i = 0;
    cJSON_ArrayForEach(it, arr) {
        if (!cJSON_IsNumber(it)) {
            return false;
        }
        double v = it->valuedouble;
        out[i++] = v < -1 ? -1 : v > I32 ? I32 : (int32_t)v;
    }
    return true;
}

bool jolly_series_parse(const char *json, size_t len, jolly_series_t *out)
{
    cJSON *root = open_doc(json, len, JOLLY_SERIES_MAX);
    if (!root) {
        return false;
    }
    jolly_series_t s;
    memset(&s, 0, sizeof(s));
    char m[8], w[8];
    bool ok = get_i64(root, "ts", 0, INT64_MAX / 2, &s.ts) && get_i64(root, "t0", 0, INT64_MAX / 2, &s.t0) &&
              get_str(root, "m", m, sizeof(m)) && !strcmp(m, "net") && get_str(root, "w", w, sizeof(w)) &&
              get_int(root, "step_s", 1, 86400, false, &s.step_s);
    if (ok && !strcmp(w, "10m")) {
        s.n = 60;
        s.has_tx = true;
        ok = get_ints(root, "rx", s.rx, 60) && get_ints(root, "tx", s.tx, 60);
    } else if (ok && !strcmp(w, "24h")) {
        s.n = 24;
        ok = get_ints(root, "rx", s.rx, 24) && get_int(root, "rx_peak_kbps", 0, I32, false, &s.rx_peak_kbps);
    } else {
        ok = false;
    }
    cJSON_Delete(root);
    if (ok) {
        *out = s;
    }
    return ok;
}

bool jolly_glance_is_new(int32_t old_seq, int32_t new_seq)
{
    return old_seq != new_seq;
}

bool jolly_etag_ok(const char *etag)
{
    size_t n = etag ? strlen(etag) : 0;
    if (n < 1 || n > 32) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (etag[i] < 0x20 || etag[i] > 0x7e) {
            return false;
        }
    }
    return true;
}

/* ---- link (§1.8) ---- */

void jolly_link_init(jolly_linkstate_t *s)
{
    memset(s, 0, sizeof(*s));
    s->link = JL_UNREACHABLE;   /* nothing heard yet */
}

void jolly_link_on_result(jolly_linkstate_t *s, int http_status, bool wifi_up, int age_s, int64_t now_ms)
{
    if (!wifi_up) {
        s->link = JL_WIFI_DOWN;
        return;
    }
    bool late = !s->have_ok || now_ms - s->last_ok_ms > LINK_TIMEOUT_MS;
    switch (http_status) {
    case 200:
    case 304:
        s->fails = 0;
        s->last_ok_ms = now_ms;
        s->have_ok = true;
        s->auth_status = 0;
        s->link = age_s > LINK_STALE_S ? JL_STALE : JL_OK;
        return;
    case 401:
    case 403:
        s->link = JL_UNAUTH;
        s->auth_status = http_status;
        return;
    case 503:   /* warming up: hold for up to 15 s */
        if (late) {
            s->link = JL_UNREACHABLE;
        }
        return;
    default:
        s->fails++;
        if (s->fails >= LINK_FAILS || late) {
            s->link = JL_UNREACHABLE;
        }
        return;
    }
}

void jolly_link_tick(jolly_linkstate_t *s, int64_t now_ms)
{
    if ((s->link == JL_OK || s->link == JL_STALE) && now_ms - s->last_ok_ms > LINK_TIMEOUT_MS) {
        s->link = JL_UNREACHABLE;
    }
}
