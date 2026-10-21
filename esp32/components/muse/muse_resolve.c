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

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"

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

static int s_forgets;   /* since boot, for >dns */

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
    s_forgets++;
    taskEXIT_CRITICAL(&s_mux);
    /* Only ours. lwIP's dns_clear_cache() used to be called here too, but it
     * completes every lookup in flight with no address (dns_call_found(i,
     * NULL)), so another task's getaddrinfo() for an unrelated name, Muse's
     * own server included, failed with EAI_FAIL. lwIP keeps an mDNS answer
     * only for its TTL anyway. */
    ESP_LOGI(TAG, "%s: asking again next time", url->host);
}

/* >dns=NAME: looks NAME up as esp-tls does (any family) and prints the
 * answer, how long it took, the DNS servers in use and how often a server
 * address was forgotten. A diagnostic for the serial console. */
bool muse_resolve_console_command(const char *line)
{
    if (strncmp(line, "dns=", 4) != 0) {
        return false;
    }
    const char *name = line + 4;
    if (!muse_dns_name_ok(name)) {
        printf("@error dns: not a host name\n");
        fflush(stdout);
        return true;
    }
    char servers[3][INET6_ADDRSTRLEN] = { "", "", "" };
    esp_netif_t *netif = esp_netif_get_default_netif();
    static const esp_netif_dns_type_t TYPES[3] = { ESP_NETIF_DNS_MAIN, ESP_NETIF_DNS_BACKUP, ESP_NETIF_DNS_FALLBACK };
    for (int i = 0; i < 3 && netif; i++) {
        esp_netif_dns_info_t info;
        if (esp_netif_get_dns_info(netif, TYPES[i], &info) == ESP_OK) {
            if (info.ip.type == ESP_IPADDR_TYPE_V4) {
                esp_ip4addr_ntoa(&info.ip.u_addr.ip4, servers[i], sizeof(servers[i]));
            }
        }
    }
    const struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res = NULL;
    int64_t t0 = esp_timer_get_time();
    int err = getaddrinfo(name, NULL, &hints, &res);
    int ms = (int)((esp_timer_get_time() - t0) / 1000);
    char ip[INET6_ADDRSTRLEN] = "";
    if (!err && res) {
        if (res->ai_family == AF_INET) {
            inet_ntop(AF_INET, &((const struct sockaddr_in *)res->ai_addr)->sin_addr, ip, sizeof(ip));
        } else {
            inet_ntop(AF_INET6, &((const struct sockaddr_in6 *)res->ai_addr)->sin6_addr, ip, sizeof(ip));
        }
    }
    if (res) {
        freeaddrinfo(res);
    }
    taskENTER_CRITICAL(&s_mux);
    int forgets = s_forgets;
    taskEXIT_CRITICAL(&s_mux);
    if (ip[0]) {
        printf("@dns {\"name\":\"%s\",\"ip\":\"%s\",\"ms\":%d", name, ip, ms);
    } else {
        printf("@dns {\"name\":\"%s\",\"error\":%d,\"ms\":%d", name, err ? err : -1, ms);
    }
    printf(",\"servers\":[\"%s\",\"%s\",\"%s\"],\"forgets\":%d}\n", servers[0], servers[1], servers[2], forgets);
    fflush(stdout);
    return true;
}
