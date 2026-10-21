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

/*
 * LCDWIKI ES3C40P: ESP32-S3 N16R8 (16 MB flash, 8 MB octal PSRAM),
 * 4.0" 320x480 ST7796S IPS SPI LCD, FT6336U capacitive touch, ES8311 codec
 * with an FM8002E speaker amp and one MEMS mic, WS2812 on GPIO42 (unused),
 * battery on GPIO9, microSD over SDIO (unused). BOOT (GPIO0) is talk.
 * Sources, all from LCDWIKI (https://www.lcdwiki.com/res/ES3C40P/):
 *   4.0inch_ES3C40P_arduino_Demo_Instructions.pdf, Table 2.1 (pp. 3-5): every
 *     pin below, Audio_EN low-level enable, FT6336U reset low.
 *   4.0inch_IPS_ESP32-S3_ES3C40P_User_Manual.pdf: ST7796S 320x480 and FT6336U
 *     (section 2); Fig. 3.21 the ES8311 (U5) at I2C 0x18 with ASDOUT on net
 *     I2S_DO (GPIO6, our DIN) and DSDIN on I2S_DI (GPIO8, our DOUT); Fig. 3.22
 *     the one mic on MIC1P/MIC1N; Fig. 3.9 the FM8002E SHUTDOWN, enabled low.
 *   ES3C40P_Specification_V1.0.pdf: module, flash and PSRAM.
 * The pins match the Freenove FNK0104B's, so this is board_fnk0104b.c with
 * the ST7796S panel (espressif/esp_lcd_st7796) in place of the ILI9341.
 * Panel settings follow a LovyanGFX config checked on this board: Panel_ST7796,
 * invert on, rgb_order false (MADCTL BGR bit set), 40 MHz SPI, rotation 0,
 * touch reporting x 0..319, y 0..479 in the visible frame. Under esp_lcd the
 * panel also needs MADCTL MX (X mirror), as the FNK0104B does: without it the
 * first flash showed the image mirrored left to right.
 * There is no vendor BSP in the ESP Component Registry, so like the AIPI
 * Lite board file this drives esp_lcd, the codec and the GPIOs directly.
 */
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "driver/usb_serial_jtag.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_codec_dev_defaults.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st7796.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "muse_audio.h"
#include "muse_board.h"
#include "muse_mem.h"
#include "es3c40p_orient.h"

static const char *TAG = "board";

/* The view LVGL draws: the 320x480 panel turned to landscape through MADCTL
 * (es3c40p_orient.h). The panel keeps its native timing. */
#define LCD_H_RES 480
#define LCD_V_RES 320
#define LCD_HOST SPI2_HOST
#define LCD_SCLK GPIO_NUM_12
#define LCD_MOSI GPIO_NUM_11
#define LCD_CS GPIO_NUM_10
#define LCD_DC GPIO_NUM_46
#define LCD_RST GPIO_NUM_NC   /* LCD_RST is the board's EN; the driver sends SWRESET */
#define LCD_BL GPIO_NUM_45    /* active high */
/* Two DMA buffers in internal RAM, 15 KB each (480 x 16 lines): the same 30 KB
 * as portrait's 320 x 24, and 320 / 16 = 20 even bands. */
#define DRAW_BUF_LINES 16

#define ORIENT_NS "jolly_grd"   /* NVS: the landscape orientation, ">lcd_orient=" */
#define ORIENT_KEY "orient"

#define I2C_SDA GPIO_NUM_16   /* shared by the FT6336U and the ES8311 */
#define I2C_SCL GPIO_NUM_15
#define TOUCH_RST GPIO_NUM_18
#define TOUCH_INT GPIO_NUM_17 /* polling only; left as an input */
#define TOUCH_ADDR 0x38
#define TOUCH_REG_TD_STATUS 0x02
#define TOUCH_REG_P1 0x03     /* XH, XL, YH, YL; high nibble of XH/YH */

#define I2S_MCLK GPIO_NUM_4
#define I2S_BCLK GPIO_NUM_5
#define I2S_DIN GPIO_NUM_6
#define I2S_DOUT GPIO_NUM_8
#define I2S_WS GPIO_NUM_7
#define PA_EN GPIO_NUM_1      /* Audio_EN: FM8002E enabled while LOW */

#define TALK_GPIO GPIO_NUM_0  /* BOOT button */
#define BATT_ADC ADC_CHANNEL_8 /* GPIO9, 1:1 divider */

static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_touch_dev;
static esp_lcd_panel_handle_t s_panel;
static volatile int s_orient;        /* es3c40p_orient.h: 0 or 1 */
static volatile bool s_touch_log;    /* ">lcd_touchlog=1": log each press, raw and mapped */
static esp_lcd_panel_io_handle_t s_io;
static muse_gpio_button_t s_talk;
static adc_oneshot_unit_handle_t s_adc;

static esp_err_t init(void)
{
    const i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_cfg, &s_i2c), TAG, "i2c");

    /* Touch controller: pulse its reset, then add it to the bus. */
    const gpio_config_t tp_rst = {
        .pin_bit_mask = (1ULL << TOUCH_RST) | (1ULL << TOUCH_INT),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&tp_rst), TAG, "touch pins");
    gpio_set_level(TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    gpio_set_direction(TOUCH_INT, GPIO_MODE_INPUT);
    const i2c_device_config_t touch_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TOUCH_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &touch_cfg, &s_touch_dev), TAG, "touch i2c");

    ESP_RETURN_ON_ERROR(muse_gpio_button_init(&s_talk, TALK_GPIO), TAG, "talk button");

    const adc_oneshot_unit_init_cfg_t adc_cfg = { .unit_id = ADC_UNIT_1 };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&adc_cfg, &s_adc), TAG, "adc");
    const adc_oneshot_chan_cfg_t ch_cfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    return adc_oneshot_config_channel(s_adc, BATT_ADC, &ch_cfg);
}

static esp_err_t touch_read_regs(uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(s_touch_dev, &reg, 1, buf, n, 50);
}

static bool s_touch_down;   /* for the touch log: one line per press */

static void touch_released(void)
{
    s_touch_down = false;
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint8_t count = 0;
    data->state = LV_INDEV_STATE_RELEASED;
    if (touch_read_regs(TOUCH_REG_TD_STATUS, &count, 1) != ESP_OK || (count & 0x0F) == 0) {
        touch_released();
        return;
    }
    uint8_t p[4];
    if (touch_read_regs(TOUCH_REG_P1, p, sizeof(p)) != ESP_OK) {
        return;
    }
    int rx = ((p[0] & 0x0F) << 8) | p[1];
    int ry = ((p[2] & 0x0F) << 8) | p[3];
    int x, y;
    es3c40p_map_touch(rx, ry, s_orient, &x, &y);   /* the FT6336U reports portrait */
    if (s_touch_log && !s_touch_down) {
        ESP_LOGI(TAG, "touch raw %d,%d -> %d,%d (orient %d)", rx, ry, x, y, s_orient);
    }
    s_touch_down = true;
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
    return;
}

static muse_board_t s_board;   /* below; display_start() places its talk hint */

static int load_orient(void)
{
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open(ORIENT_NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u8(h, ORIENT_KEY, &v) != ESP_OK || v >= ES3C40P_ORIENTS) {
            v = 0;
        }
        nvs_close(h);
    }
    return v;
}

static esp_err_t save_orient(int orient)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ORIENT_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, ORIENT_KEY, (uint8_t)orient);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    return err;
}

static void apply_madctl(es3c40p_madctl_t m)
{
    esp_lcd_panel_swap_xy(s_panel, m.swap_xy);
    esp_lcd_panel_mirror(s_panel, m.mirror_x, m.mirror_y);
}

static lv_display_t *display_start(lv_indev_t **touch)
{
    const ledc_timer_config_t bl_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 20000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t bl_ch = {
        .gpio_num = LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    if (ledc_timer_config(&bl_timer) != ESP_OK || ledc_channel_config(&bl_ch) != ESP_OK) {
        return NULL;
    }

    const spi_bus_config_t bus = {
        .sclk_io_num = LCD_SCLK,
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_H_RES * DRAW_BUF_LINES * 2,
    };
    if (spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        return NULL;
    }
    esp_lcd_panel_io_handle_t io;
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_CS,
        .dc_gpio_num = LCD_DC,
        .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    if (esp_lcd_new_panel_io_spi(LCD_HOST, &io_cfg, &io) != ESP_OK) {
        return NULL;
    }
    s_io = io;
    /* No vendor_config: the driver's default ST7796 init sequence. BGR is
     * LovyanGFX's rgb_order = false. */
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    if (esp_lcd_new_panel_st7796(io, &panel_cfg, &s_panel) != ESP_OK) {
        return NULL;
    }
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, true);   /* IPS panel: LovyanGFX invert = true */
    /* Landscape: MADCTL swaps and mirrors the panel's addressing below LVGL
     * (es3c40p_madctl), and the touch is turned to match (es3c40p_map_touch). */
    s_orient = load_orient();
    apply_madctl(es3c40p_madctl(s_orient));
    if (s_orient == 1) {
        /* BOOT, at the portrait's lower left, is now at the top left. The UI
         * places the hint once, so a live change moves it at the next start. */
        s_board.talk_hint = (muse_button_hint_t){ LV_ALIGN_TOP_LEFT, 8, 8 };
    }
    esp_lcd_panel_disp_on_off(s_panel, true);

    esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_cfg.task_core_id = MUSE_UI_CORE;
    adapter_cfg.task_priority = MUSE_UI_PRIORITY;
    if (esp_lv_adapter_init(&adapter_cfg) != ESP_OK) {
        return NULL;
    }
    const esp_lv_adapter_display_config_t disp_cfg = {
        .panel = s_panel,
        .panel_io = io,
        .profile = {
            .interface = ESP_LV_ADAPTER_PANEL_IF_OTHER,
            .rotation = ESP_LV_ADAPTER_ROTATE_0,
            .hor_res = LCD_H_RES,
            .ver_res = LCD_V_RES,
            .buffer_height = DRAW_BUF_LINES,
            .use_psram = false,
            .require_double_buffer = true,
        },
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
    };
    lv_display_t *disp = esp_lv_adapter_register_display(&disp_cfg);
    if (!disp) {
        return NULL;
    }

    /* No esp_lcd_touch driver for the FT6336U in the registry setup here, so
     * register a plain LVGL pointer indev that polls it over I2C. */
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev, disp);
    lv_indev_set_read_cb(indev, touch_read_cb);
    *touch = indev;

    if (esp_lv_adapter_start() != ESP_OK) {
        return NULL;
    }
    return disp;
}

static bool display_lock(int timeout_ms)
{
    return esp_lv_adapter_lock(timeout_ms) == ESP_OK;
}

static void set_brightness(int pct)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, pct * 1023 / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

/*
 * The UI has already turned the backlight off (and turns it back on after).
 * espressif/esp_lcd_st7796 has no disp_sleep ("sleep is not supported by this
 * panel"), so send the ST7796S's SLPIN/SLPOUT ourselves; GRAM is kept. Its
 * datasheet wants 5 ms after SLPIN and 120 ms after SLPOUT.
 */
static void panel_sleep(bool sleep)
{
    if (s_io && esp_lcd_panel_io_tx_param(s_io, sleep ? LCD_CMD_SLPIN : LCD_CMD_SLPOUT, NULL, 0) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(sleep ? 5 : 120));
    }
}

/* One ES8311 does both directions over a duplex I2S bus, MCLK from its pin. */
static esp_err_t audio_init(esp_codec_dev_handle_t *spk, esp_codec_dev_handle_t *mic)
{
    i2s_chan_handle_t tx, rx;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx, &rx), TAG, "i2s channel");
    const i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MUSE_AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK,
            .bclk = I2S_BCLK,
            .ws = I2S_WS,
            .dout = I2S_DOUT,
            .din = I2S_DIN,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx, &std_cfg), TAG, "i2s tx");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx, &std_cfg), TAG, "i2s rx");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx), TAG, "i2s tx on");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(rx), TAG, "i2s rx on");

    audio_codec_i2s_cfg_t i2s_cfg = { .port = I2S_NUM_0, .rx_handle = rx, .tx_handle = tx };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    audio_codec_i2c_cfg_t i2c_cfg = { .port = I2C_NUM_0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = s_i2c };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(data_if && ctrl_if && gpio_if, ESP_ERR_NO_MEM, TAG, "codec interfaces");

    es8311_codec_cfg_t es_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = PA_EN,
        .pa_reverted = true,    /* Audio_EN: low enables, high disables */
        .use_mclk = true,
        .hw_gain = { .pa_voltage = 5.0, .codec_dac_voltage = 3.3 },
    };
    const audio_codec_if_t *codec = es8311_codec_new(&es_cfg);
    ESP_RETURN_ON_FALSE(codec, ESP_FAIL, TAG, "ES8311 not responding");

    esp_codec_dev_cfg_t out_cfg = { .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = codec, .data_if = data_if };
    esp_codec_dev_cfg_t in_cfg = { .dev_type = ESP_CODEC_DEV_TYPE_IN, .codec_if = codec, .data_if = data_if };
    *spk = esp_codec_dev_new(&out_cfg);
    *mic = esp_codec_dev_new(&in_cfg);
    return *spk && *mic ? ESP_OK : ESP_FAIL;
}

static unsigned poll_buttons(void)
{
    return muse_gpio_button_poll(&s_talk);
}

/* Battery voltage on GPIO9 (BAT_ADC) through a 1:1 divider: the user manual's
 * battery circuit says to multiply the ADC voltage by 2. */
static esp_err_t read_power(muse_power_t *out)
{
    int sum = 0;
    for (int i = 0; i < 8; i++) {
        int v;
        ESP_RETURN_ON_ERROR(adc_oneshot_read(s_adc, BATT_ADC, &v), TAG, "adc read");
        sum += v;
    }
    int mv = sum / 8 * 3300 * 2 / 4095;   /* uncalibrated counts */
    int pct = (mv - 3300) * 100 / 900;
    out->battery_pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
    out->battery_mv = mv;
    out->charging = false;
    out->usb = usb_serial_jtag_is_connected();
    return ESP_OK;
}

static esp_err_t power_off(void)
{
    set_brightness(0);
    esp_lcd_panel_disp_on_off(s_panel, false);
    while (gpio_get_level(TALK_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_RETURN_ON_ERROR(esp_sleep_enable_ext0_wakeup(TALK_GPIO, 0), TAG, "wake button");
    esp_deep_sleep_start();
    return ESP_FAIL;
}

/* The I2C header's bus (SDA 16, SCL 15), for an add-on IMU (jolly_imu). */
static void *i2c_bus(void)
{
    return s_i2c;
}

/* Live: under LVGL's lock, so no flush is mid-way, then the whole screen redrawn. */
static bool apply_live(es3c40p_madctl_t m)
{
    if (!s_panel || esp_lv_adapter_lock(1000) != ESP_OK) {
        return false;
    }
    apply_madctl(m);
    lv_obj_invalidate(lv_screen_active());
    esp_lv_adapter_unlock();
    return true;
}

static bool parse_bit(const char *s, char **end, bool *out)
{
    long v = strtol(s, end, 10);
    if (*end == s || (v != 0 && v != 1)) {
        return false;
    }
    *out = v;
    return true;
}

/*
 * Bench commands for finding the upright landscape in one flash:
 *   lcd                 the orientation and MADCTL in use
 *   lcd_orient=0|1      sets it, saves it and applies it now (touch too)
 *   lcd_madctl=S,X,Y    applies swap/mirror_x/mirror_y now, unsaved; touch unchanged
 *   lcd_touchlog=0|1    logs each press, raw and mapped
 * A line starting "lcd_" that isn't one of these is refused (muse_secret.h).
 */
static bool board_console(const char *line)
{
    if (!strcmp(line, "lcd")) {
        es3c40p_madctl_t m = es3c40p_madctl(s_orient);
        printf("@lcd orient %d swap %d mirror_x %d mirror_y %d\n", s_orient, m.swap_xy, m.mirror_x, m.mirror_y);
        fflush(stdout);
        return true;
    }
    if (!strncmp(line, "lcd_orient=", 11)) {
        char *end;
        long v = strtol(line + 11, &end, 10);
        if (end == line + 11 || *end || v < 0 || v >= ES3C40P_ORIENTS) {
            printf("@lcd_orient error want 0 or 1\n");
        } else {
            s_orient = (int)v;
            es3c40p_madctl_t m = es3c40p_madctl(s_orient);
            bool live = apply_live(m);
            esp_err_t err = save_orient(s_orient);
            ESP_LOGI(TAG, "orientation %d (swap %d mirror %d,%d)%s%s", s_orient, m.swap_xy, m.mirror_x, m.mirror_y,
                     live ? "" : ", applies at restart", err == ESP_OK ? "" : ", not saved");
            printf("@lcd_orient %s %d\n", err == ESP_OK ? "ok" : "error storage", s_orient);
        }
        fflush(stdout);
        return true;
    }
    if (!strncmp(line, "lcd_madctl=", 11)) {
        es3c40p_madctl_t m;
        char *p = (char *)line + 11, *end;
        bool ok = parse_bit(p, &end, &m.swap_xy) && *end == ',' && parse_bit(end + 1, &end, &m.mirror_x) &&
                  *end == ',' && parse_bit(end + 1, &end, &m.mirror_y) && !*end;
        if (!ok) {
            printf("@lcd_madctl error want S,X,Y as 0 or 1\n");
        } else if (apply_live(m)) {
            ESP_LOGI(TAG, "MADCTL swap %d mirror %d,%d (bench, not saved)", m.swap_xy, m.mirror_x, m.mirror_y);
            printf("@lcd_madctl ok %d,%d,%d\n", m.swap_xy, m.mirror_x, m.mirror_y);
        } else {
            printf("@lcd_madctl error display busy\n");
        }
        fflush(stdout);
        return true;
    }
    if (!strcmp(line, "lcd_touchlog=0") || !strcmp(line, "lcd_touchlog=1")) {
        s_touch_log = line[13] == '1';
        printf("@lcd_touchlog ok %d\n", s_touch_log);
        fflush(stdout);
        return true;
    }
    return false;
}

static muse_board_t s_board = {
    .name = "LCDWIKI ES3C40P",
    .width = LCD_H_RES,
    .height = LCD_V_RES,
    .console = board_console,
    .i2c_bus = i2c_bus,
    .round = false,
    .touch = true,
    .diagonal_in = 4.0f,
    .talk_button = "boot",
    .talk_hint = { LV_ALIGN_BOTTOM_RIGHT, -8, -8 },   /* BOOT, for orientation 0; init() moves it for 1 */
    .screen_talk_px = 72,   /* BOOT is on the edge: a talk button on the screen too */
    .frame_ms = 40,
    .init = init,
    .display_start = display_start,
    .display_lock = display_lock,
    .display_unlock = esp_lv_adapter_unlock,
    .set_brightness = set_brightness,
    .panel_sleep = panel_sleep,
    .audio_init = audio_init,
    .mic_slot = 0,              /* one mic, on the left slot */
    .poll_buttons = poll_buttons,
    .read_power = read_power,
    .power_off = power_off,
};

/* Home Link's app_main starts Muse with this board (main/main.c). */
const muse_board_t *muse_board_get(void)
{
    return &s_board;
}
