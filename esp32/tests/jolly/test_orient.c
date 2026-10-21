/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * es3c40p_orient: the FT6336U's portrait coordinates (0..319 x 0..479, the
 * frame that matched the portrait image) to the 480x320 landscape view, and
 * the MADCTL bits for each orientation.
 */
#include <assert.h>
#include <stdio.h>

#include "boards/es3c40p_orient.h"

static void map(int rx, int ry, int orient, int ex, int ey)
{
    int x = -1, y = -1;
    es3c40p_map_touch(rx, ry, orient, &x, &y);
    if (x != ex || y != ey) {
        fprintf(stderr, "orient %d (%d,%d) -> (%d,%d), want (%d,%d)\n", orient, rx, ry, x, y, ex, ey);
        assert(0);
    }
}

static void touch_map_both_orientations(void)
{
    /* Orientation 0: the portrait top becomes the left, its right edge the top. */
    map(0, 0, 0, 0, 319);
    map(319, 479, 0, 479, 0);
    map(160, 240, 0, 240, 159);
    map(319, 0, 0, 0, 0);
    map(0, 479, 0, 479, 319);
    /* Orientation 1: the same image turned 180 degrees. */
    map(0, 0, 1, 479, 0);
    map(319, 479, 1, 0, 319);
    map(160, 240, 1, 239, 160);
    /* Out of range raw values are clamped to the panel. */
    map(-5, 600, 0, 479, 319);
    map(400, -1, 0, 0, 0);
    map(400, -1, 1, 479, 319);
    /* An unknown orientation behaves as 0. */
    map(0, 0, 7, 0, 319);
}

static void madctl_table(void)
{
    es3c40p_madctl_t m0 = es3c40p_madctl(0), m1 = es3c40p_madctl(1), mx = es3c40p_madctl(9);
    assert(m0.swap_xy && !m0.mirror_x && !m0.mirror_y);
    assert(m1.swap_xy && m1.mirror_x && m1.mirror_y);
    assert(mx.swap_xy == m0.swap_xy && mx.mirror_x == m0.mirror_x && mx.mirror_y == m0.mirror_y);
}

int main(void)
{
    touch_map_both_orientations();
    madctl_table();
    return 0;
}
