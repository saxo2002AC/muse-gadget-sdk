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

#include "jolly/jolly_imu.h"
#include "jolly/jolly_face_spec.h"

#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#include "jolly/jolly_eyes.h"
#include "jolly/jolly_motion.h"
#include "muse_mem.h"
#include "muse_state.h"

static const char *TAG = "jolly_imu";

#define ADDR 0x68
#define REG_SMPLRT_DIV 0x19
#define REG_CONFIG 0x1A
#define REG_ACCEL_CONFIG 0x1C
#define REG_ACCEL_XOUT_H 0x3B
#define REG_PWR_MGMT_1 0x6B
#define REG_PWR_MGMT_2 0x6C
#define REG_WHO_AM_I 0x75
#define LSB_PER_G 4096             /* +-8 g: impacts reach past 2.5 g */
#define PERIOD_MS 10               /* 100 Hz */
#define RETRY_MS 1000              /* after a run of failed reads */
#define FAILS_BEFORE_RETRY 50

static i2c_master_dev_handle_t s_dev;
static bool s_present;
static uint8_t s_who;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static int16_t s_mg[3];            /* the last sample, milli-g */
static jolly_motion_mood_t s_mood;
EXT_RAM_BSS_ATTR static jolly_motion_t s_motion;   /* ~300 B of window, in PSRAM */

static const char *const EV_NAMES[JME_COUNT] = { "", "shake", "freefall", "landing", "knock", "face-down", "face-up" };

static esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    uint8_t b[2] = { reg, value };
    return i2c_master_transmit(s_dev, b, sizeof(b), 50);
}

static esp_err_t read_regs(uint8_t reg, uint8_t *out, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, out, n, 50);
}

static void imu_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    int fails = 0;
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(fails >= FAILS_BEFORE_RETRY ? RETRY_MS : PERIOD_MS));
        uint8_t raw[6];
        if (read_regs(REG_ACCEL_XOUT_H, raw, sizeof(raw)) != ESP_OK) {
            if (++fails == FAILS_BEFORE_RETRY) {
                ESP_LOGW(TAG, "MPU-6050 stopped answering: trying once a second");
            }
            continue;
        }
        if (fails >= FAILS_BEFORE_RETRY) {
            ESP_LOGI(TAG, "MPU-6050 back");
        }
        fails = 0;
        int16_t mg[3];
        for (int i = 0; i < 3; i++) {
            int16_t v = (int16_t)((raw[2 * i] << 8) | raw[2 * i + 1]);
            mg[i] = (int16_t)((int32_t)v * 1000 / LSB_PER_G);
        }
        int64_t now = esp_timer_get_time() / 1000;
        /* A knock counts while Jolly is idle or being touched. */
        bool gate = muse_state_mode(NULL) == MUSE_MODE_IDLE || jolly_eyes_touched();
        jolly_motion_ev_t ev = jolly_motion_feed(&s_motion, mg[0], mg[1], mg[2], now, gate);
        taskENTER_CRITICAL(&s_mux);
        memcpy(s_mg, mg, sizeof(s_mg));
        if (ev != JME_NONE) {
            jolly_motion_mood_on(&s_mood, ev, now);
        }
        taskEXIT_CRITICAL(&s_mux);
        if (ev != JME_NONE) {
            ESP_LOGI(TAG, "%s", EV_NAMES[ev]);
        }
    }
}

bool jolly_imu_start(void *bus)
{
    jolly_motion_mood_init(&s_mood);
    jolly_motion_init(&s_motion);
    i2c_master_bus_handle_t b = bus;
    if (!b || i2c_master_probe(b, ADDR, 50) != ESP_OK) {
        ESP_LOGI(TAG, "no MPU-6050 at 0x%02x: no motion moods", ADDR);
        return false;
    }
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ADDR,
        .scl_speed_hz = 400000,
    };
    if (i2c_master_bus_add_device(b, &cfg, &s_dev) != ESP_OK) {
        return false;
    }
    read_regs(REG_WHO_AM_I, &s_who, 1);   /* 0x68 on an MPU-6050; clones differ, so only logged */
    /* Awake on its own clock, gyro off (only the accelerometer is used), 100 Hz
     * samples (1 kHz / (1 + 9)) through the 184 Hz filter, which keeps knocks. */
    esp_err_t err = write_reg(REG_PWR_MGMT_1, 0x00);
    err |= write_reg(REG_PWR_MGMT_2, 0x07);
    err |= write_reg(REG_CONFIG, 0x01);
    err |= write_reg(REG_SMPLRT_DIV, 9);
    err |= write_reg(REG_ACCEL_CONFIG, 0x10);   /* +-8 g */
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MPU-6050 at 0x%02x didn't take its setup", ADDR);
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return false;
    }
    /* Stack in PSRAM: it never writes flash. */
    if (xTaskCreatePinnedToCoreWithCaps(imu_task, "jolly_imu", 3072, NULL, 3, NULL, 0, MUSE_BIG_CAPS) != pdPASS) {
        ESP_LOGE(TAG, "start failed: no motion moods");
        return false;
    }
    s_present = true;
    ESP_LOGI(TAG, "MPU-6050 at 0x%02x (who 0x%02x): motion moods on", ADDR, s_who);
    return true;
}

uint32_t jolly_imu_flashes(void)
{
    if (!s_present) {
        return 0;
    }
    taskENTER_CRITICAL(&s_mux);
    uint32_t n = s_mood.flashes;
    taskEXIT_CRITICAL(&s_mux);
    return n;
}

jolly_mood_t jolly_imu_mood(int64_t now_ms)
{
    if (!s_present) {
        return JM_TRANQUILO;
    }
    taskENTER_CRITICAL(&s_mux);
    jolly_mood_t m = jolly_motion_mood_now(&s_mood, now_ms);
    taskEXIT_CRITICAL(&s_mux);
    return m;
}

bool jolly_imu_console_command(const char *line)
{
    if (strcmp(line, "imu") != 0) {
        return false;
    }
    int16_t mg[3];
    taskENTER_CRITICAL(&s_mux);
    memcpy(mg, s_mg, sizeof(mg));
    taskEXIT_CRITICAL(&s_mux);
    jolly_mood_t m = jolly_imu_mood(esp_timer_get_time() / 1000);
    printf("@imu {\"present\":%s,\"who\":\"0x%02x\",\"g\":[%.3f,%.3f,%.3f],\"mood\":\"%s\"}\n",
           s_present ? "true" : "false", s_who, mg[0] / 1000.0, mg[1] / 1000.0, mg[2] / 1000.0,
           s_present && m != JM_TRANQUILO ? jolly_face_name(m) : "");
    fflush(stdout);
    return true;
}
