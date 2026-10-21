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

/* The Guardián client's surface for the simulator, which has no network:
 * the link is fine, or down with MUSE_SIM_JOLLY_LINK=down, and no glance
 * has arrived. */
#include <stdlib.h>
#include <string.h>

#include "jolly/jolly_client.h"

bool jolly_client_snapshot(jolly_glance_t *glance, jolly_linkstate_t *link)
{
    (void)glance;
    const char *which = getenv("MUSE_SIM_JOLLY_LINK");
    memset(link, 0, sizeof(*link));
    link->link = which && !strcmp(which, "down") ? JL_UNREACHABLE : JL_OK;
    return false;
}
