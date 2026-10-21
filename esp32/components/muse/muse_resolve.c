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

#include "muse_resolve.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/tcpip.h"

static const char *TAG = "muse_resolve";

#define CACHE 2   /* the TTS server and the Guardián */

typedef struct {
    char host[MUSE_URL_HOST_MAX];
    char ip[16];
} entry_t;

static entry_t s_cache[CACHE];
static int s_next;   /* the entry the next new host replaces */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static bool cached(const char *host, char ip[16])
{
    bool hit = false;
    taskENTER_CRITICAL(&s_mux);
    for (int i = 0; i < CACHE; i++) {
        if (s_cache[i].host[0] && !strcmp(s_cache[i].host, host)) {
            memcpy(ip, s_cache[i].ip, 16);
            hit = true;
            break;
        }
    }
    taskEXIT_CRITICAL(&s_mux);
    return hit;
}

static void remember(const char *host, const char *ip)
{
    taskENTER_CRITICAL(&s_mux);
    int slot = -1;
    for (int i = 0; i < CACHE; i++) {
        if (!strcmp(s_cache[i].host, host)) {
            slot = i;
        }
    }
    if (slot < 0) {
        slot = s_next;
        s_next = (s_next + 1) % CACHE;
    }
    strlcpy(s_cache[slot].host, host, sizeof(s_cache[slot].host));
    strlcpy(s_cache[slot].ip, ip, sizeof(s_cache[slot].ip));
    taskEXIT_CRITICAL(&s_mux);
}

bool muse_resolve(const muse_url_t *url, char ip[16])
{
    if (!url->mdns) {
        strlcpy(ip, url->host, 16);
        return true;
    }
    if (cached(url->host, ip)) {
        return true;
    }
    const struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res = NULL;
    int err = getaddrinfo(url->host, NULL, &hints, &res);
    if (err || !res) {
        ESP_LOGW(TAG, "can't find %s by mDNS (%d)", url->host, err);
        if (res) {
            freeaddrinfo(res);
        }
        return false;
    }
    const struct sockaddr_in *sa = (const struct sockaddr_in *)res->ai_addr;
    inet_ntoa_r(sa->sin_addr, ip, 16);
    freeaddrinfo(res);
    remember(url->host, ip);
    ESP_LOGI(TAG, "%s is %s", url->host, ip);
    return true;
}

static void clear_lwip_cache(void *arg)
{
    (void)arg;
    dns_clear_cache();   /* lwIP's own, on its thread */
}

void muse_resolve_forget(const muse_url_t *url)
{
    if (!url->mdns) {
        return;
    }
    taskENTER_CRITICAL(&s_mux);
    for (int i = 0; i < CACHE; i++) {
        if (!strcmp(s_cache[i].host, url->host)) {
            s_cache[i].host[0] = '\0';
        }
    }
    taskEXIT_CRITICAL(&s_mux);
    tcpip_callback(clear_lwip_cache, NULL);
    ESP_LOGI(TAG, "%s: asking again next time", url->host);
}
