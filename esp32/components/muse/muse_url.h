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

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Local servers' URLs (the TTS server, the Guardián): http://host:port and
 * nothing else, the host an IPv4 address or a one-label .local name such as
 * Mac-mini-de-acserver.local, found by mDNS since the Mini's address may
 * change (muse_resolve.h). Pure, so tests/jolly/test_url.c checks it.
 */

#define MUSE_URL_HOST_MAX 70   /* a 63-character label + ".local" */

typedef struct {
    char host[MUSE_URL_HOST_MAX];
    int port;
    bool mdns;                 /* a .local name */
} muse_url_t;

/* False for anything else: another scheme, no port, a path, a DNS name, a
 * user, spaces or control characters. */
bool muse_url_parse(const char *url, muse_url_t *out);

#ifdef __cplusplus
}
#endif
