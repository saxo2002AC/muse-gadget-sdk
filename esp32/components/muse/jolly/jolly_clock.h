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
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Jolly's clock: SNTP (pool.ntp.org, time.cloudflare.com, through the DNS
 * servers muse_dns.h sets) once Wi-Fi is up, re-synced every 6 h
 * (CONFIG_LWIP_SNTP_UPDATE_DELAY), in Orlando's time with daylight saving
 * (EST5EDT,M3.2.0,M11.1.0).
 */

#define JOLLY_CLOCK_TZ "EST5EDT,M3.2.0,M11.1.0"
#define JOLLY_CLOCK_H12 true       /* "3:45 pm": Orlando's way */

/* Sets the time zone and starts watching for Wi-Fi to start SNTP (once). */
void jolly_clock_start(void);

/* SNTP has set the time (the atajo signs requests with it). */
bool jolly_clock_synced(void);

/* The status bar's text, "3:45 pm", or "--:--" until synced. */
void jolly_clock_text(char *out, size_t cap);

/* The serial console's >time: @time {synced, utc, local, last_sync_s}.
 * False for any other line. */
bool jolly_clock_console_command(const char *line);

#ifdef __cplusplus
}
#endif
