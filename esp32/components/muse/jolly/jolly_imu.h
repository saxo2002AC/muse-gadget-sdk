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
#include <stdint.h>

#include "jolly/jolly_mood.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * An optional MPU-6050 (a GY-521 board) on the I2C header, at 0x68 beside
 * the touch controller (0x38) and the codec (0x18). Probed once at start:
 * without it, nothing changes. With it, a small task reads the accelerometer
 * at 100 Hz and jolly_motion.c turns what it feels into moods.
 *
 * Console: ">imu" prints @imu {"present":...,"who":"0x68","g":[x,y,z],"mood":"..."}.
 */

/* `bus` is the board's i2c_master_bus_handle_t (muse_board_t.i2c_bus), or NULL. */
bool jolly_imu_start(void *bus);

/* The mood the motion asks for now (jolly_mood_in_t.motion); JM_TRANQUILO
 * for none, or without an IMU. */
jolly_mood_t jolly_imu_mood(int64_t now_ms);

/* How many landings and knocks so far (0 without an IMU): a change is one to
 * flash the screen for, whatever mood shows. */
uint32_t jolly_imu_flashes(void);

bool jolly_imu_console_command(const char *line);

#ifdef __cplusplus
}
#endif
