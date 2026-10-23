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

#include "jolly/jolly_led.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "led_strip.h"
#include "nvs.h"

static const char *TAG = "jolly_led";

#define LED_GPIO 42                /* the ES3C40P's WS2812 (LCDWIKI spec) */
#define NVS_NS "jolly_led"

static led_strip_handle_t s_strip;
static volatile int s_brightness = JOLLY_LED_DEFAULT_BRIGHTNESS;
static volatile bool s_beat;       /* off by default: an all-fine board stays dark */
static jolly_rgb_t s_shown = { 1, 1, 1 };   /* forces the first write */

void jolly_led_start(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        uint8_t v;
        if (nvs_get_u8(h, "bright", &v) == ESP_OK && v <= 100) {
            s_brightness = v;
        }
        if (nvs_get_u8(h, "beat", &v) == ESP_OK) {
            s_beat = v == 1;
        }
        nvs_close(h);
    }
    const led_strip_config_t strip = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    const led_strip_rmt_config_t rmt = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    if (led_strip_new_rmt_device(&strip, &rmt, &s_strip) != ESP_OK) {
        ESP_LOGW(TAG, "no RMT for the LED on GPIO%d", LED_GPIO);
        s_strip = NULL;
        return;
    }
    led_strip_clear(s_strip);
    ESP_LOGI(TAG, "status LED on GPIO%d, brightness %d%%%s", LED_GPIO, s_brightness, s_beat ? ", heartbeat" : "");
}

void jolly_led_show(const jolly_led_in_t *state)
{
    if (!s_strip) {
        return;
    }
    jolly_led_in_t in = *state;
    in.brightness = s_brightness;
    in.beat = s_beat;
    jolly_rgb_t c = jolly_led_color(&in);
    if (c.r == s_shown.r && c.g == s_shown.g && c.b == s_shown.b) {
        return;   /* only a change goes out */
    }
    s_shown = c;
    if (led_strip_set_pixel(s_strip, 0, c.r, c.g, c.b) == ESP_OK) {
        led_strip_refresh(s_strip);
    }
}

static bool save_u8(const char *key, uint8_t v)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    bool ok = nvs_set_u8(h, key, v) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

static bool number(const char *v, long lo, long hi, long *out)
{
    char *end = NULL;
    long n = strtol(v, &end, 10);
    if (!*v || *end || strlen(v) > 3 || n < lo || n > hi) {
        return false;
    }
    *out = n;
    return true;
}

bool jolly_led_console_command(const char *line)
{
    long n;
    if (!strncmp(line, "led=", 4)) {
        if (!number(line + 4, 0, 100, &n)) {
            printf("@led error invalid (want 0..100)\n");
        } else if (!save_u8("bright", (uint8_t)n)) {
            printf("@led error storage\n");
        } else {
            s_brightness = (int)n;
            printf("@led ok\n");
        }
        fflush(stdout);
        return true;
    }
    if (!strncmp(line, "led_beat=", 9)) {
        if (!number(line + 9, 0, 1, &n)) {
            printf("@led_beat error invalid (want 0 or 1)\n");
        } else if (!save_u8("beat", (uint8_t)n)) {
            printf("@led_beat error storage\n");
        } else {
            s_beat = n == 1;
            printf("@led_beat ok\n");
        }
        fflush(stdout);
        return true;
    }
    if (!strcmp(line, "led")) {
        printf("@led {\"present\":%s,\"brightness\":%d,\"beat\":%d,\"rgb\":[%u,%u,%u]}\n", s_strip ? "true" : "false",
               s_brightness, s_beat ? 1 : 0, s_shown.r, s_shown.g, s_shown.b);
        fflush(stdout);
        return true;
    }
    return false;
}
