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

#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Jolly's status pages, after the guardian mockup v3 (landscape 480x320):
 * Mac Mini, Apps, Red and Seguridad, as tiles after Jolly's in the UI's
 * tileview, each with the same status bar. They show what the Guardián
 * client (jolly_client) last read; server text is the parsers' sanitized
 * strings, in labels without recolour (R16). Call all of these on the LVGL
 * task.
 */

#define JOLLY_PAGES 4

/* Adds the four tiles at columns first_col .. first_col + 3 of `tv`. */
void jolly_pages_create(lv_obj_t *tv, int first_col);

/* The status chip on Jolly's own tile. */
void jolly_pages_face(lv_obj_t *face);

/* The tile now showing: 1..JOLLY_PAGES for ours, 0 for any other. Asks the
 * client for that page's data and plays its entry animation. */
void jolly_pages_show(int page);

/* Once a frame: refreshes the chips and the page showing. */
void jolly_pages_tick(int64_t now_ms);

#ifdef __cplusplus
}
#endif
