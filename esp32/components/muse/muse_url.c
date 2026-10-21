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

#include "muse_url.h"

#include <string.h>
#include <strings.h>

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool is_alnum(char c)
{
    return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* Four dot-separated numbers of 1 to 3 digits, each up to 255. */
static bool ipv4(const char *s, size_t n)
{
    int parts = 0;
    size_t i = 0;
    while (i < n) {
        int v = 0, digits = 0;
        while (i < n && is_digit(s[i])) {
            v = v * 10 + (s[i++] - '0');
            if (++digits > 3) {
                return false;
            }
        }
        if (!digits || v > 255) {
            return false;
        }
        parts++;
        if (i < n) {
            if (s[i] != '.' || parts == 4) {
                return false;
            }
            i++;
            if (i == n) {
                return false;   /* a trailing dot */
            }
        }
    }
    return parts == 4;
}

/* One label of letters, digits and inner hyphens, then ".local". */
static bool local_name(const char *s, size_t n)
{
    static const char SUFFIX[] = ".local";
    size_t label = n > sizeof(SUFFIX) - 1 ? n - (sizeof(SUFFIX) - 1) : 0;
    if (!label || label > 63 || strncasecmp(s + label, SUFFIX, sizeof(SUFFIX) - 1) != 0) {
        return false;
    }
    if (s[0] == '-' || s[label - 1] == '-') {
        return false;
    }
    for (size_t i = 0; i < label; i++) {
        if (!is_alnum(s[i]) && s[i] != '-') {
            return false;
        }
    }
    return true;
}

bool muse_url_parse(const char *url, muse_url_t *out)
{
    static const char SCHEME[] = "http://";
    if (!url || strncmp(url, SCHEME, sizeof(SCHEME) - 1) != 0) {
        return false;
    }
    const char *host = url + sizeof(SCHEME) - 1;
    const char *colon = strchr(host, ':');
    if (!colon) {
        return false;
    }
    size_t n = (size_t)(colon - host);
    if (!n || n >= MUSE_URL_HOST_MAX) {
        return false;
    }
    bool mdns = false;
    if (!ipv4(host, n)) {
        if (!local_name(host, n)) {
            return false;
        }
        mdns = true;
    }
    const char *p = colon + 1;
    long port = 0;
    int digits = 0;
    while (is_digit(*p)) {
        port = port * 10 + (*p++ - '0');
        if (++digits > 5) {
            return false;
        }
    }
    if (!digits || *p || port < 1 || port > 65535) {
        return false;
    }
    memcpy(out->host, host, n);
    out->host[n] = '\0';
    out->port = (int)port;
    out->mdns = mdns;
    return true;
}
