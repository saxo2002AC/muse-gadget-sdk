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

#include "jolly/jolly_glance.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The Guardián client: polls the Mac Mini's Guardián (the guardian plan's
 * contract v1) and keeps the latest glance and the link state for the UI.
 *
 * The URL and key live in NVS (namespace jolly_grd), set on the serial
 * console only, never over BLE:
 *   >grd_url=http://Mac-mini-de-acserver.local:3025   (or an IPv4 address)
 *   >grd_key=<key>                                     (never echoed or logged)
 *   >grd                                               the URL, key length, link
 * An empty value clears either. The key goes in X-Guardian-Key.
 */

/* Optional endpoints, fetched only while a page shows them. */
#define JOLLY_WANT_APPS 0x1
#define JOLLY_WANT_EVENTS 0x2
#define JOLLY_WANT_SERIES_10M 0x4
#define JOLLY_WANT_SERIES_24H 0x8

/* Loads the URL and key and starts polling. Safe to call once at start. */
void jolly_client_start(void);

/* Handles a console line without its '>'; false if it isn't one of these. */
bool jolly_client_console_command(const char *line);

/* A copy of the latest glance and link (thread-safe). False if no glance
 * has arrived yet; *link is filled either way. */
bool jolly_client_snapshot(jolly_glance_t *glance, jolly_linkstate_t *link);

/* Which optional endpoints to poll (JOLLY_WANT_*), for the visible page. */
void jolly_client_want(unsigned mask);

/* The latest optional data; false if none has arrived since it was wanted. */
bool jolly_client_apps(jolly_apps_t *out);
bool jolly_client_events(jolly_events_t *out);
bool jolly_client_series(bool day, jolly_series_t *out);

#ifdef __cplusplus
}
#endif
