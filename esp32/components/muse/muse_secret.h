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
 * Commands that carry secrets.
 *
 * The TTS key, the SDK token and the Guardián key are set on the serial
 * console only, by their own handlers. A line in those families that no
 * handler took (a typo, no '=', too long to read whole) must not reach the
 * setup commands (muse_ble_command), which log what they're given.
 */

/* True if `line` (a console line without its '>') is in a console-only
 * secret family: tts_*, sdk_token*, grd_*. "tts" alone is the status. */
bool muse_secret_console_only(const char *line);

/* True if the setup or console command named `cmd` (without its '=value')
 * carries a secret whose value must never be logged or echoed. */
bool muse_secret_value(const char *cmd);

#ifdef __cplusplus
}
#endif
