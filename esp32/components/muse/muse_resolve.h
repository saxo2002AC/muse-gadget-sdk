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

#include "muse_url.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A local server's IPv4 address, for muse_url_t hosts. An IPv4 host is used
 * as is. A .local name is asked for with lwIP's one-shot mDNS query
 * (CONFIG_LWIP_DNS_SUPPORT_MDNS_QUERIES; no mDNS task or component) and the
 * answer is kept until muse_resolve_forget(), which the clients call after a
 * failed connect: the Mini's DHCP address may have changed.
 */

/* `ip` gets the dotted address (16 bytes). False if it can't be found. Blocks
 * for the mDNS query the first time; call it from a worker task. */
bool muse_resolve(const muse_url_t *url, char ip[16]);

/* Drops our cached address of a .local host, so the next muse_resolve()
 * asks again. lwIP's own cache is left alone (see muse_resolve.c). */
void muse_resolve_forget(const muse_url_t *url);

/* Our DNS servers ahead of the router's (muse_dns.h): from NVS (>dns_servers=)
 * or CONFIG_MUSE_DNS_SERVERS, put in on every lease and checked every 5 s
 * against DHCP renewals. Call once; it waits for the network to come up. */
void muse_dns_start(void);

/* Resolves `host` (as esp-tls will), up to 3 tries 0.3 s then 1 s apart, so a
 * flaky DNS server doesn't fail a turn. Blocks: worker tasks only. */
bool muse_resolve_warm(const char *host);

/* The serial console's >dns=NAME: prints @dns {name, ip | error, ms,
 * servers, ours, forgets}; and >dns_servers=a.b.c.d[,a.b.c.d] (empty: the
 * router's only), kept in NVS. False for any other line. */
bool muse_resolve_console_command(const char *line);

#ifdef __cplusplus
}
#endif
