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

/* muse_dns_name_ok: what >dns= may look up (and so print back). */
static void dns_names(void)
{
    assert(muse_dns_name_ok("hatch.metaaivm.com"));
    assert(muse_dns_name_ok("Mac-mini-de-acserver.local"));
    assert(muse_dns_name_ok("a"));
    assert(muse_dns_name_ok("x1.y-2.z"));
    assert(!muse_dns_name_ok(""));
    assert(!muse_dns_name_ok("bad name.com"));
    assert(!muse_dns_name_ok("evil\n.com"));
    assert(!muse_dns_name_ok("a\"b.com"));
    assert(!muse_dns_name_ok("-lead.com"));
    assert(!muse_dns_name_ok("trail-.com"));
    assert(!muse_dns_name_ok("double..dot"));
    assert(!muse_dns_name_ok(".lead"));
    assert(!muse_dns_name_ok("http://x.com"));
    char longlabel[70];
    memset(longlabel, 'a', 64);
    longlabel[64] = '\0';
    assert(!muse_dns_name_ok(longlabel));   /* a label is at most 63 */
    char longname[300];
    for (int i = 0; i < 254; i++) {
        longname[i] = i % 2 ? '.' : 'a';
    }
    longname[253] = 'a';
    longname[254] = '\0';
    assert(!muse_dns_name_ok(longname));    /* a name is at most 253 */
    longname[253] = '\0';
    assert(muse_dns_name_ok(longname));
}

/* muse_host_port_parse: Muse's server as stored ("host", "host:port"), read
 * forgivingly: a trailing ':' (an empty port) means the default. */
static void host_port(const char *in, bool ok_want, const char *host_want, int port_want, bool clean_want)
{
    char host[64];
    int port = -1;
    bool clean = !clean_want;
    bool ok = muse_host_port_parse(in, 443, host, sizeof(host), &port, &clean);
    if (ok != ok_want || (ok && (strcmp(host, host_want) || port != port_want || clean != clean_want))) {
        fprintf(stderr, "%s: ok %d host '%s' port %d clean %d\n", in, ok, ok ? host : "", port, clean);
        assert(0);
    }
}

static void host_ports(void)
{
    host_port("hatch.metaaivm.com:", true, "hatch.metaaivm.com", 443, false);      /* Andrés's board */
    host_port("hatch.metaaivm.com:443", true, "hatch.metaaivm.com", 443, true);
    host_port("hatch.metaaivm.com", true, "hatch.metaaivm.com", 443, true);
    host_port("[::1]:443", true, "::1", 443, true);
    host_port("[::1]", true, "::1", 443, true);
    host_port("[::1]:", true, "::1", 443, false);
    host_port("example.org:8443", true, "example.org", 8443, true);
    host_port(" hatch.metaaivm.com ", true, "hatch.metaaivm.com", 443, false);   /* stray spaces */
    host_port("10.0.0.5:9000", true, "10.0.0.5", 9000, true);
    host_port("", false, "", 0, true);
    host_port(":", false, "", 0, true);
    host_port(":443", false, "", 0, true);
    host_port("host:0", false, "", 0, true);
    host_port("host:65536", false, "", 0, true);
    host_port("host:44a", false, "", 0, true);
    host_port("ho st", false, "", 0, true);
    host_port("[::1", false, "", 0, true);
    host_port("::1", false, "", 0, true);                 /* IPv6 needs brackets */
    host_port("https://hatch.metaaivm.com", false, "", 0, true);
    char longer[80];
    memset(longer, 'a', 70);
    longer[70] = '\0';
    host_port(longer, false, "", 0, true);               /* too long for the buffer */
}

int main(void)
{
    host_ports();
    dns_names();
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
