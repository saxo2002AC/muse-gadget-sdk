/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * muse_hang_stalled: which watched clock stopped for longer than its limit.
 */
#include <assert.h>

#include "muse_hang.h"

int main(void)
{
    int64_t last[MUSE_HANG_COUNT] = { 0, 0 };
    assert(muse_hang_stalled(last, 1000000) == -1);                 /* never kicked: not watched yet */
    last[MUSE_HANG_UI] = 1000;
    last[MUSE_HANG_APP] = 1000;
    assert(muse_hang_stalled(last, 1000 + MUSE_HANG_UI_MS) == -1);   /* at the limit: fine */
    assert(muse_hang_stalled(last, 1001 + MUSE_HANG_UI_MS) == MUSE_HANG_UI);
    last[MUSE_HANG_UI] = 1000 + MUSE_HANG_APP_MS;                    /* the UI ticks; Link's heartbeat doesn't */
    assert(muse_hang_stalled(last, 1001 + MUSE_HANG_APP_MS) == MUSE_HANG_APP);
    assert(MUSE_HANG_UI_MS < MUSE_HANG_APP_MS);
    return 0;
}
