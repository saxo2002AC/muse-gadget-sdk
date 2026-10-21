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
 * The ES3C40P in landscape: the ST7796S turned to a 480x320 view through
 * MADCTL (swap and mirror), and its FT6336U touch turned with it. Pure, so
 * tests/jolly/test_orient.c checks it on the host.
 *
 * Orientation 0 puts the portrait top on the left; 1 is the same turned 180
 * degrees. Which one is upright depends on how the board stands, so it's a
 * setting (">lcd_orient=0|1" on the console, kept in NVS).
 */

#define ES3C40P_PORTRAIT_W 320   /* the panel and touch, as they report */
#define ES3C40P_PORTRAIT_H 480
#define ES3C40P_ORIENTS 2

typedef struct {
    bool swap_xy, mirror_x, mirror_y;
} es3c40p_madctl_t;

/* MADCTL for `orient` (an unknown one is 0). Derived from the portrait
 * image, which needed mirror_x alone; the console's ">lcd_madctl" tries
 * other combinations on the bench if this one shows mirrored. */
es3c40p_madctl_t es3c40p_madctl(int orient);

/* The touch controller's portrait point (clamped to the panel) to the
 * landscape view's (0..479, 0..319). */
void es3c40p_map_touch(int raw_x, int raw_y, int orient, int *x, int *y);

#ifdef __cplusplus
}
#endif
