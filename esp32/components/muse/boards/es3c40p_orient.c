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

#include "es3c40p_orient.h"

static int clamp(int v, int max)
{
    return v < 0 ? 0 : v > max ? max : v;
}

es3c40p_madctl_t es3c40p_madctl(int orient)
{
    /*
     * Portrait showed upright with only MX set: the panel's columns run right
     * to left. With MV the view's x walks the rows and its y the columns, so
     * landscape 0 (portrait top on the left) needs neither mirror, and 1, the
     * 180-degree turn, needs both.
     */
    if (orient == 1) {
        return (es3c40p_madctl_t){ .swap_xy = true, .mirror_x = true, .mirror_y = true };
    }
    return (es3c40p_madctl_t){ .swap_xy = true, .mirror_x = false, .mirror_y = false };
}

void es3c40p_map_touch(int raw_x, int raw_y, int orient, int *x, int *y)
{
    int rx = clamp(raw_x, ES3C40P_PORTRAIT_W - 1);
    int ry = clamp(raw_y, ES3C40P_PORTRAIT_H - 1);
    if (orient == 1) {
        *x = ES3C40P_PORTRAIT_H - 1 - ry;
        *y = rx;
    } else {
        *x = ry;
        *y = ES3C40P_PORTRAIT_W - 1 - rx;
    }
}
