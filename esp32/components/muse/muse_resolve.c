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

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/dns.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/priv/tcpip_priv.h"
#include "muse_dns.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "muse_resolve";

#ifndef CONFIG_MUSE_DNS_SERVERS
#define CONFIG_MUSE_DNS_SERVERS ""   /* boards without Muse's settings (the Voice PE) */
#endif

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

/* ---- Our DNS servers ahead of the router's (muse_dns.h) ---- */

#define DNS_NS "muse_dns"
#define KEEP_US (5LL * 1000 * 1000)    /* DHCP renewals set the router's again, silently */

static SemaphoreHandle_t s_dns_lock;
static muse_dns_state_t s_dns;
static esp_timer_handle_t s_dns_timer;

/* The servers lwIP uses, read and set on its own thread. */
typedef struct {
    struct tcpip_api_call_data call;
    uint32_t v[3];   /* host order, 0 for none */
} dns_call_t;

static err_t read_servers(struct tcpip_api_call_data *c)
{
    dns_call_t *d = (dns_call_t *)c;
    for (int i = 0; i < 3; i++) {
        const ip_addr_t *a = dns_getserver((u8_t)i);
        d->v[i] = a && IP_IS_V4(a) ? lwip_ntohl(ip_2_ip4(a)->addr) : 0;
    }
    return ERR_OK;
}

static err_t write_servers(struct tcpip_api_call_data *c)
{
    dns_call_t *d = (dns_call_t *)c;
    for (int i = 0; i < 3; i++) {
        ip_addr_t a;
        ip_addr_set_ip4_u32_val(a, lwip_htonl(d->v[i]));
        dns_setserver((u8_t)i, d->v[i] ? &a : NULL);
    }
    return ERR_OK;
}

/* lwIP's thread and the event loop exist once an interface does: before
 * that (Muse can start first) a call into lwIP would wait on a queue that
 * isn't there. The count is a plain read, no call into lwIP. */
static bool net_up(void)
{
    return esp_netif_get_nr_of_ifs() > 0;
}

/* On lwIP's thread: read the servers, put ours back if DHCP replaced them. */
static void keep_on_tcpip(void *arg)
{
    (void)arg;
    dns_call_t now = { 0 }, want = { 0 };
    read_servers(&now.call);
    xSemaphoreTake(s_dns_lock, portMAX_DELAY);
    bool apply = muse_dns_step(&s_dns, now.v, want.v);
    xSemaphoreGive(s_dns_lock);
    if (apply) {
        write_servers(&want.call);
        char a[3 * 16 + 4];
        muse_dns_format(want.v, want.v[2] ? 3 : want.v[1] ? 2 : 1, a, sizeof(a));
        ESP_LOGI(TAG, "DNS servers: %s", a);
    }
}

/* Never waits on lwIP: it's called from the esp_timer task and the event
 * loop, which must not stall behind a busy lwIP thread. A full mailbox just
 * skips this round; the next one comes 5 s later. */
static void dns_keep(void)
{
    if (!s_dns_lock || !net_up()) {
        return;
    }
    tcpip_try_callback(keep_on_tcpip, NULL);
}

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;
    dns_keep();   /* a new lease: DHCP just set the router's */
}

static void on_keep_timer(void *arg)
{
    (void)arg;
    static bool registered;
    if (!registered && net_up()) {
        /* The default event loop is up by now: hear each new lease at once. */
        registered = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_got_ip, NULL, NULL) ==
                     ESP_OK;
    }
    dns_keep();
}

/* Ours: NVS if ever set (empty there means the router's only), else Kconfig. */
static void load_dns(uint32_t ours[MUSE_DNS_MAX], int *n)
{
    char text[MUSE_DNS_TEXT];
    strlcpy(text, CONFIG_MUSE_DNS_SERVERS, sizeof(text));
    nvs_handle_t h;
    if (nvs_open(DNS_NS, NVS_READONLY, &h) == ESP_OK) {
        char saved[MUSE_DNS_TEXT];
        size_t len = sizeof(saved);
        if (nvs_get_str(h, "servers", saved, &len) == ESP_OK) {
            strlcpy(text, saved, sizeof(text));
        }
        nvs_close(h);
    }
    if (!muse_dns_parse(text, ours, n)) {
        ESP_LOGW(TAG, "DNS servers \"%s\" aren't a list of IPv4 addresses; using the router's", text);
        *n = 0;
    }
}

void muse_dns_start(void)
{
    if (s_dns_lock) {
        return;
    }
    uint32_t ours[MUSE_DNS_MAX];
    int n;
    load_dns(ours, &n);
    muse_dns_init(&s_dns, ours, n);
    s_dns_lock = xSemaphoreCreateMutex();
    const esp_timer_create_args_t t = { .callback = on_keep_timer, .name = "dns_keep" };
    if (esp_timer_create(&t, &s_dns_timer) == ESP_OK) {
        esp_timer_start_periodic(s_dns_timer, KEEP_US);
    }
    char text[MUSE_DNS_TEXT];
    muse_dns_format(ours, n, text, sizeof(text));
    ESP_LOGI(TAG, "DNS: %s, then the router's", n ? text : "the router's only");
}

bool muse_resolve_warm(const char *host)
{
    static const int WAIT_MS[] = { 0, 300, 1000 };
    for (int i = 0; i < 3; i++) {
        if (WAIT_MS[i]) {
            vTaskDelay(pdMS_TO_TICKS(WAIT_MS[i]));
        }
        const struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
        struct addrinfo *res = NULL;
        int err = getaddrinfo(host, NULL, &hints, &res);
        if (res) {
            freeaddrinfo(res);
        }
        if (!err) {
            return true;   /* lwIP has it cached for esp-tls's own lookup */
        }
        ESP_LOGW(TAG, "can't resolve %s (%d), try %d of 3", host, err, i + 1);
    }
    return false;
}

static bool dns_servers_command(const char *v)
{
    uint32_t ours[MUSE_DNS_MAX];
    int n;
    if (strlen(v) >= MUSE_DNS_TEXT || !muse_dns_parse(v, ours, &n)) {
        printf("@dns_servers error invalid (want a.b.c.d[,a.b.c.d], or empty for the router's)\n");
        fflush(stdout);
        return true;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(DNS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_str(h, "servers", v);
        err = err == ESP_OK ? nvs_commit(h) : err;
        nvs_close(h);
    }
    if (err != ESP_OK) {
        printf("@dns_servers error storage\n");
        fflush(stdout);
        return true;
    }
    if (s_dns_lock) {
        xSemaphoreTake(s_dns_lock, portMAX_DELAY);
        muse_dns_set(&s_dns, ours, n);
        xSemaphoreGive(s_dns_lock);
        dns_keep();
    }
    printf("@dns_servers ok\n");
    fflush(stdout);
    return true;
}

/* >dns=NAME: looks NAME up as esp-tls does (any family) and prints the
 * answer, how long it took, the DNS servers in use and how often a server
 * address was forgotten. A diagnostic for the serial console. */
bool muse_resolve_console_command(const char *line)
{
    if (!strncmp(line, "dns_servers=", 12)) {
        return dns_servers_command(line + 12);
    }
    if (strncmp(line, "dns=", 4) != 0) {
        return false;
    }
    const char *name = line + 4;
    if (!muse_dns_name_ok(name)) {
        printf("@error dns: not a host name\n");
        fflush(stdout);
        return true;
    }
    dns_call_t now = { 0 };
    if (net_up()) {
        tcpip_api_call(read_servers, &now.call);
    }
    char servers[3][16];
    for (int i = 0; i < 3; i++) {
        muse_dns_format(&now.v[i], now.v[i] ? 1 : 0, servers[i], sizeof(servers[i]));
    }
    char ours[MUSE_DNS_TEXT] = "";
    if (s_dns_lock) {
        xSemaphoreTake(s_dns_lock, portMAX_DELAY);
        muse_dns_format(s_dns.ours, s_dns.n, ours, sizeof(ours));
        xSemaphoreGive(s_dns_lock);
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
    printf(",\"servers\":[\"%s\",\"%s\",\"%s\"],\"ours\":\"%s\",\"forgets\":%d}\n", servers[0], servers[1],
           servers[2], ours, forgets);
    fflush(stdout);
    return true;
}
