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

#include "jolly/jolly_clock.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "jolly/jolly_text.h"
#include "muse_wifi.h"

static const char *TAG = "jolly_clock";

#define WATCH_US (2LL * 1000 * 1000)   /* looks for Wi-Fi every 2 s until SNTP starts */

static esp_timer_handle_t s_watch;
static _Atomic bool s_started, s_synced;
static _Atomic int64_t s_synced_us;      /* esp_timer time of the last sync */

static void on_sync(struct timeval *tv)
{
    (void)tv;
    bool first = !atomic_exchange(&s_synced, true);
    atomic_store(&s_synced_us, esp_timer_get_time());
    char t[16];
    jolly_clock_text(t, sizeof(t));
    ESP_LOGI(TAG, "time synced%s: %s local", first ? "" : " again", t);
}

/* SNTP once Wi-Fi is up: its server names go through our DNS servers. The
 * interface count is a plain read; only then is lwIP's thread there. */
static void on_watch(void *arg)
{
    (void)arg;
    if (atomic_load(&s_started) || esp_netif_get_nr_of_ifs() == 0 || !muse_wifi_connected()) {
        return;
    }
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2, ESP_SNTP_SERVER_LIST("pool.ntp.org",
                                                                                         "time.cloudflare.com"));
    cfg.sync_cb = on_sync;
    if (esp_netif_sntp_init(&cfg) != ESP_OK) {
        ESP_LOGW(TAG, "SNTP didn't start; trying again");
        return;
    }
    atomic_store(&s_started, true);
    esp_timer_stop(s_watch);
    ESP_LOGI(TAG, "SNTP: pool.ntp.org, time.cloudflare.com, every %d h", CONFIG_LWIP_SNTP_UPDATE_DELAY / 3600000);
}

void jolly_clock_start(void)
{
    setenv("TZ", JOLLY_CLOCK_TZ, 1);
    tzset();
    if (s_watch) {
        return;
    }
    const esp_timer_create_args_t t = { .callback = on_watch, .name = "clock_watch" };
    if (esp_timer_create(&t, &s_watch) == ESP_OK) {
        esp_timer_start_periodic(s_watch, WATCH_US);
    }
}

void jolly_clock_text(char *out, size_t cap)
{
    time_t now = time(NULL);
    struct tm lt;
    bool ok = atomic_load(&s_synced) && localtime_r(&now, &lt);
    jolly_fmt_clock(ok, ok ? lt.tm_hour : 0, ok ? lt.tm_min : 0, JOLLY_CLOCK_H12, out, cap);
}

bool jolly_clock_console_command(const char *line)
{
    if (strcmp(line, "time") != 0) {
        return false;
    }
    time_t now = time(NULL);
    struct tm u, l;
    char utc[32] = "", local[40] = "";
    gmtime_r(&now, &u);
    localtime_r(&now, &l);
    strftime(utc, sizeof(utc), "%Y-%m-%dT%H:%M:%SZ", &u);
    strftime(local, sizeof(local), "%Y-%m-%d %H:%M:%S %Z", &l);
    bool synced = atomic_load(&s_synced);
    long ago = synced ? (long)((esp_timer_get_time() - atomic_load(&s_synced_us)) / 1000000) : -1;
    printf("@time {\"synced\":%s,\"sntp\":%s,\"utc\":\"%s\",\"local\":\"%s\",\"last_sync_s\":%ld}\n",
           synced ? "true" : "false", atomic_load(&s_started) ? "true" : "false", utc, local, ago);
    fflush(stdout);
    return true;
}
