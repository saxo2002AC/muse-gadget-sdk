/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * muse_url_parse: server URLs are http://host:port only, the host an IPv4
 * address or one-label .local name (mDNS).
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "muse_url.h"

static void ok(const char *url, const char *host, int port, bool mdns)
{
    muse_url_t u;
    if (!muse_url_parse(url, &u) || strcmp(u.host, host) || u.port != port || u.mdns != mdns) {
        fprintf(stderr, "should accept %s\n", url);
        assert(0);
    }
}

static void bad(const char *url)
{
    muse_url_t u;
    if (muse_url_parse(url, &u)) {
        fprintf(stderr, "should refuse %s\n", url);
        assert(0);
    }
}

int main(void)
{
    ok("http://192.168.0.11:3024", "192.168.0.11", 3024, false);
    ok("http://0.0.0.0:1", "0.0.0.0", 1, false);
    ok("http://255.255.255.255:65535", "255.255.255.255", 65535, false);
    ok("http://Mac-mini-de-acserver.local:3025", "Mac-mini-de-acserver.local", 3025, true);
    ok("http://box.LOCAL:80", "box.LOCAL", 80, true);
    ok("http://a.local:1", "a.local", 1, true);

    bad(NULL);
    bad("");
    bad("https://192.168.0.11:3024");
    bad("HTTP://192.168.0.11:3024");
    bad("http://192.168.0.11");            /* the port is required */
    bad("http://192.168.0.11:");
    bad("http://192.168.0.11:0");
    bad("http://192.168.0.11:65536");
    bad("http://192.168.0.11:8a");
    bad("http://192.168.0.11:+80");
    bad("http://192.168.0.11:3024/");      /* no path */
    bad("http://192.168.0.11:3024/tts");
    bad("http://192.168.0.256:1");
    bad("http://192.168.0:1");
    bad("http://192.168.0.1.2:1");
    bad("http://1..2.3:1");
    bad("http://1234.1.1.1:1");
    bad("http://example.com:80");          /* plain DNS names are not allowed */
    bad("http://mini:80");
    bad("http://a.b.local:1");             /* one label before .local */
    bad("http://.local:1");
    bad("http://-a.local:1");
    bad("http://a-.local:1");
    bad("http://a_b.local:1");
    bad("http://a b.local:1");
    bad("http://[::1]:80");
    bad("http://user@1.2.3.4:80");
    bad("http://1.2.3.4:80\r\nX: y");
    char longname[100];
    memset(longname, 'a', 64);
    strcpy(longname + 64, ".local:1");
    char url[120];
    snprintf(url, sizeof(url), "http://%s", longname);
    bad(url);                              /* a 64-character label */
    return 0;
}
