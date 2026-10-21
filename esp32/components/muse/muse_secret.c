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

#include "muse_secret.h"

#include <string.h>

static bool starts(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

bool muse_secret_console_only(const char *line)
{
    return starts(line, "tts_") || starts(line, "sdk_token") || starts(line, "grd_") || starts(line, "lcd_");
}

bool muse_secret_value(const char *cmd)
{
    return !strcmp(cmd, "wifi.pass") || starts(cmd, "hatch.token") || muse_secret_console_only(cmd);
}
