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

/* The Guardián client's surface for the simulator, which has no network.
 * MUSE_SIM_JOLLY_LINK=down: no link. MUSE_SIM_JOLLY_DATA=ok|crit: data as in
 * tests/jolly/contract/glance.{ok,crit}.json (and apps, events, series), for
 * screenshots of the status pages; unset, no glance has arrived. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jolly/jolly_client.h"

static char s_kind[8];   /* a scenario's jolly=...: wins over the environment */

/* ok, crit, spy (a security warning), quiet (quiet hours), down (no link), none. */
void sim_jolly_set(const char *kind)
{
    snprintf(s_kind, sizeof(s_kind), "%s", kind);
}

static const char *kind_now(void)
{
    if (s_kind[0]) {
        return s_kind;
    }
    const char *d = getenv("MUSE_SIM_JOLLY_DATA");
    return d ? d : "";
}

static const char *data_kind(void)
{
    const char *d = kind_now();
    return !strcmp(d, "ok") || !strcmp(d, "crit") || !strcmp(d, "spy") || !strcmp(d, "quiet") ? d : NULL;
}

bool jolly_client_snapshot(jolly_glance_t *g, jolly_linkstate_t *link)
{
    const char *which = getenv("MUSE_SIM_JOLLY_LINK");
    memset(link, 0, sizeof(*link));
    link->link = (which && !strcmp(which, "down")) || !strcmp(kind_now(), "down") ? JL_UNREACHABLE : JL_OK;
    link->have_ok = true;
    const char *d = data_kind();
    if (!d || !g) {
        return false;
    }
    bool crit = !strcmp(d, "crit");
    memset(g, 0, sizeof(*g));
    g->ts = 1792634820;
    g->seq = 18342;
    g->age_s = 3;
    g->level = crit ? JLV_CRIT : JLV_OK;
    g->crit = crit;
    g->warn = crit ? 2 : 0;
    strcpy(g->head, crit ? "Respaldo externo: hace 10 días" : "");
    g->mac.up_s = 5788800;
    g->mac.cpu = 23;
    g->mac.temp = 48;
    g->mac.therm = JT_NORMAL;
    g->mac.mem = JLV_OK;
    g->mac.swap_mb = 2662;
    g->mac.swap_tot_mb = 4096;
    g->mac.disk_pct = 5;
    g->mac.disk_free_gb = 266;
    strcpy(g->mac.macos, "26.7.1");
    g->mac.backup_state = crit ? JS_CRIT : JS_OK;
    g->mac.backup_age_s = crit ? 864000 : 3600;
    g->mac.monitor_state = JS_OK;
    g->mac.monitor_age_s = 120;
    g->apps.up = crit ? 24 : 25;
    g->apps.total = 25;
    if (crit) {
        g->apps.ndown = 1;
        strcpy(g->apps.down[0].id, "scanner-disparadores");
        g->apps.down[0].port = 3020;
        g->apps.down[0].for_s = 180;
    }
    g->net.lat_ms = 14;
    g->net.rx_kbps = 8400;
    g->net.tx_kbps = 1200;
    g->net.cf = 4;
    g->net.cf_exp = 4;
    strcpy(g->net.ts_state, "up");
    g->net.ts_online = 3;
    g->net.has_speed = true;
    g->net.speed_down = 312;
    g->net.speed_up = 41;
    g->net.speed_age_s = 2400;
    g->sec.level = crit || !strcmp(d, "spy") ? JLV_WARN : JLV_OK;
    g->quiet = !strcmp(d, "quiet");
    g->sec.ports = 15;
    g->sec.ports_unreg = 3;
    g->sec.plists = 71;
    g->sec.ts_nodes = 3;
    return true;
}

void jolly_client_want(unsigned mask)
{
    (void)mask;
}

bool jolly_client_apps(jolly_apps_t *a)
{
    static const char *const IDS[] = { "scalp", "secondleg", "exitsense", "opti-trade", "nq-detector",
                                       "panel", "licitemos", "mision-puntos", "scanner-disparadores",
                                       "runner", "wetrade", "greenleaf", "siteproof", "muse-voz", "guardian",
                                       "serventi", "minio", "postgres-ui", "grafana", "auditkit", "docs",
                                       "fotos", "consulado", "hotmail-sync", "monitor-web" };
    if (!data_kind()) {
        return false;
    }
    memset(a, 0, sizeof(*a));
    a->n = (int)(sizeof(IDS) / sizeof(IDS[0]));
    for (int i = 0; i < a->n; i++) {
        strcpy(a->apps[i].id, IDS[i]);
        a->apps[i].port = 3000 + i;
        a->apps[i].st = i == 0 ? JA_DEP : (i == 8 && !strcmp(data_kind(), "crit")) ? JA_DOWN : JA_UP;
    }
    return true;
}

static void set_ev(jolly_events_t *e, int i, int64_t t, jolly_level_t sev, const char *cat, const char *txt, bool open)
{
    e->ev[i].t = t;
    e->ev[i].sev = sev;
    strcpy(e->ev[i].cat, cat);
    strcpy(e->ev[i].txt, txt);
    e->ev[i].open = open;
}

bool jolly_client_events(jolly_events_t *e)
{
    if (!data_kind()) {
        return false;
    }
    memset(e, 0, sizeof(*e));
    e->ts = 1792634820;
    e->n = 3;
    set_ev(e, 0, 1792634100, JLV_CRIT, "plist", "Servicio nuevo en arranque: com.unknown.helper.plist", true);
    set_ev(e, 1, 1792630000, JLV_WARN, "port", "Puerto sin registro: 8787 (node)", true);
    set_ev(e, 2, 1792620000, JLV_OK, "ssh", "SSH: 0 intentos fallidos hoy", false);
    return true;
}

bool jolly_client_series(bool day, jolly_series_t *s)
{
    if (!data_kind()) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->n = day ? 24 : 60;
    s->has_tx = !day;
    for (int i = 0; i < s->n; i++) {
        s->rx[i] = day ? (i < 2 ? -1 : 3000 + (i * 1700) % 9000) : 6000 + (i * 1300) % 7000;
        s->tx[i] = day ? 0 : 800 + (i * 370) % 1700;
    }
    s->rx_peak_kbps = 46000;
    return true;
}

/* No IMU in the simulator. */
#include "jolly/jolly_imu.h"

uint32_t jolly_imu_flashes(void)
{
    return 0;
}

jolly_mood_t jolly_imu_mood(int64_t now_ms)
{
    (void)now_ms;
    return JM_TRANQUILO;
}
