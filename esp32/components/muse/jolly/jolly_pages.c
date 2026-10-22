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

#include "jolly/jolly_pages.h"

#include <stdio.h>
#include <string.h>

#if __has_include("esp_attr.h")
#include "esp_attr.h"
#endif
#ifndef EXT_RAM_BSS_ATTR
#define EXT_RAM_BSS_ATTR
#endif

#include "jolly/jolly_client.h"
#include "jolly/jolly_text.h"
#include "muse_ui.h"

LV_FONT_DECLARE(jolly_font_12)
LV_FONT_DECLARE(jolly_font_16)
LV_FONT_DECLARE(jolly_font_20)
LV_FONT_DECLARE(jolly_font_28)

/* The mockup's tokens. */
#define C_BG 0x0B0D1A
#define C_CARD 0x161A2E
#define C_CARD2 0x1E2340
#define C_LINE 0x2A3054
#define C_TEXT 0xEEF1FF
#define C_MUTED 0xA3AACB
#define C_ACCENT 0x5CE1FF
#define C_ACCENT2 0xB9A6FF
#define C_OK 0x3DDC97
#define C_WARN 0xFFC145
#define C_CRIT 0xFF5C6C
#define C_CRIT_INK 0xFF9AA4
#define C_STALE 0x8A90A8
#define C_OK_TINT 0x10291F
#define C_WARN_TINT 0x2E2512
#define C_CRIT_TINT 0x33141D
#define C_STALE_TINT 0x1E2030

#define W 480
#define H 320
#define PAD 12
#define TOP 68                     /* below the status bar and the title row */
#define BOTTOM 294                 /* above the page dots */
#define REFRESH_MS 1000            /* "hace N s" and the like */
#define COUNT_MS 400               /* numbers counting up as a page appears */
#define FILL_MS 500                /* meters filling */
#define APP_PILLS JOLLY_APPS_N
#define EVENT_ROWS 4

/* The latest data, copied from the client: in PSRAM, not internal RAM. */
EXT_RAM_BSS_ATTR static jolly_glance_t s_g;
EXT_RAM_BSS_ATTR static jolly_apps_t s_apps;
EXT_RAM_BSS_ATTR static jolly_events_t s_events;
EXT_RAM_BSS_ATTR static jolly_series_t s_s10, s_s24;
static jolly_linkstate_t s_link;
static bool s_have;
static int s_page;                 /* showing: 0 none, 1..4 */
static int64_t s_next_refresh, s_now;

typedef struct {
    lv_obj_t *bg, *lbl;
    int tone;                      /* last set, -1 for none: only a change restyles */
} chip_t;

EXT_RAM_BSS_ATTR static chip_t s_chips[JOLLY_PAGES + 1];   /* [0] on Jolly's tile */

/* ---- Building blocks ---- */

static lv_obj_t *box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t bg, int32_t radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE);   /* a double tap anywhere reaches the tile */
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    return o;
}

static lv_obj_t *card(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    return box(parent, x, y, w, h, C_CARD, 12);
}

/* A label of server or fixed text. Never with recolour (R16). */
static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *t)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_label_set_text(l, t);
    return l;
}

/* Only a changed string redraws. */
static void set_text(lv_obj_t *l, const char *t)
{
    if (strcmp(lv_label_get_text(l), t) != 0) {
        lv_label_set_text(l, t);
    }
}

/* One line, cut with "…": a fixed height, or LVGL wraps instead. */
static void one_line(lv_obj_t *l, int32_t w)
{
    lv_obj_set_size(l, w, lv_font_get_line_height(lv_obj_get_style_text_font(l, 0)));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
}

static void set_color(lv_obj_t *o, uint32_t c)
{
    lv_obj_set_style_text_color(o, lv_color_hex(c), 0);
}

static void tone_colors(jolly_tone_t tone, uint32_t *bg, uint32_t *fg)
{
    static const uint32_t BG[] = { C_OK_TINT, C_WARN_TINT, C_CRIT_TINT, C_STALE_TINT };
    static const uint32_t FG[] = { C_OK, C_WARN, C_CRIT_INK, C_STALE };
    *bg = BG[tone];
    *fg = FG[tone];
}

/* A tap on the chip opens the page that explains it: Seguridad for a
 * security warning or crit, else Mac Mini. */
static int s_first_col = 1;        /* the Mac Mini page's tileview column */

static void on_chip(lv_event_t *e)
{
    (void)e;
    bool sec = s_have && s_g.sec.level >= JLV_WARN;
    muse_ui_show_tile(s_first_col + (sec ? 3 : 0));
}

static chip_t chip(lv_obj_t *parent)
{
    chip_t c = { .tone = -1 };
    c.bg = lv_obj_create(parent);
    lv_obj_remove_style_all(c.bg);
    lv_obj_remove_flag(c.bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c.bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(c.bg, 10);   /* 22 px tall: easier to hit */
    lv_obj_add_event_cb(c.bg, on_chip, LV_EVENT_CLICKED, NULL);
    lv_obj_set_size(c.bg, LV_SIZE_CONTENT, 22);
    lv_obj_set_style_radius(c.bg, 11, 0);
    lv_obj_set_style_bg_opa(c.bg, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(c.bg, 9, 0);
    c.lbl = text(c.bg, &jolly_font_12, C_STALE, "");
    lv_obj_center(c.lbl);
    return c;
}

static void chip_set(chip_t *c, const char *t, jolly_tone_t tone)
{
    if (c->tone != (int)tone) {
        uint32_t bg, fg;
        tone_colors(tone, &bg, &fg);
        lv_obj_set_style_bg_color(c->bg, lv_color_hex(bg), 0);
        set_color(c->lbl, fg);
        c->tone = (int)tone;
    }
    set_text(c->lbl, t);
}

/* The status bar: Wi-Fi on the left, the chip on the right. No clock: the
 * board keeps no wall time. */
static void status_bar(lv_obj_t *tile, chip_t *c)
{
    lv_obj_t *wifi = text(tile, &lv_font_montserrat_14, C_MUTED, LV_SYMBOL_WIFI);
    lv_obj_set_pos(wifi, PAD, 7);
    *c = chip(tile);
    lv_obj_align(c->bg, LV_ALIGN_TOP_RIGHT, -PAD, 3);
}

static lv_obj_t *title(lv_obj_t *tile, const char *t)
{
    lv_obj_t *l = text(tile, &jolly_font_20, C_TEXT, t);
    lv_obj_set_pos(l, PAD, 36);
    return l;
}

/* A big number with its unit after it, counting up when the page appears. */
typedef struct {
    lv_obj_t *num, *unit;
    int32_t value;                 /* JOLLY_NONE shows "—" */
} big_t;

static big_t big(lv_obj_t *parent, int32_t x, int32_t y, const char *unit)
{
    big_t b = { .value = JOLLY_NONE };
    b.num = text(parent, &jolly_font_28, C_TEXT, "—");
    lv_obj_set_pos(b.num, x, y);
    b.unit = text(parent, &jolly_font_12, C_MUTED, unit);
    lv_obj_align_to(b.unit, b.num, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, -4);
    return b;
}

static void big_show(big_t *b, int32_t v)
{
    char s[16];
    if (v == JOLLY_NONE) {
        snprintf(s, sizeof(s), "—");
    } else {
        snprintf(s, sizeof(s), "%d", (int)v);
    }
    set_text(b->num, s);
    lv_obj_align_to(b->unit, b->num, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, -4);
}

static void count_cb(void *var, int32_t v)
{
    big_show((big_t *)var, v);
}

static void big_set(big_t *b, int32_t v, bool animate)
{
    b->value = v;
    if (animate && v != JOLLY_NONE && v > 0) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, b);
        lv_anim_set_exec_cb(&a, count_cb);
        lv_anim_set_values(&a, 0, v);
        lv_anim_set_duration(&a, COUNT_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    } else {
        lv_anim_delete(b, count_cb);
        big_show(b, v);
    }
}

static lv_obj_t *meter(lv_obj_t *parent, int32_t x, int32_t y, int32_t w)
{
    lv_obj_t *m = lv_bar_create(parent);
    lv_obj_remove_flag(m, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(m, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_pos(m, x, y);
    lv_obj_set_size(m, w, 6);
    lv_bar_set_range(m, 0, 100);
    lv_obj_set_style_bg_color(m, lv_color_hex(C_CARD2), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(m, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(m, lv_color_hex(C_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(m, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(m, 3, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(m, FILL_MS, LV_PART_MAIN);
    return m;
}

static void on_double_tap(lv_event_t *e)
{
    (void)e;
    muse_ui_show_face();   /* back to Jolly */
}

static lv_obj_t *page_tile(lv_obj_t *tv, int col)
{
    lv_obj_t *t = lv_tileview_add_tile(tv, (uint8_t)col, 0, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(t, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(t, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(t, on_double_tap, LV_EVENT_DOUBLE_CLICKED, NULL);
    return t;
}

/* ---- Page 1: Mac Mini ---- */

EXT_RAM_BSS_ATTR static struct {
    lv_obj_t *up_bg, *up_lbl;
    lv_obj_t *banner, *bk, *bv, *bd;
    lv_obj_t *os, *mon;
    big_t cpu, temp, disk;
    lv_obj_t *cpu_meter, *therm, *mem, *swap, *free_gb, *foot;
} s_mac;

static void build_mac(lv_obj_t *t)
{
    status_bar(t, &s_chips[1]);
    title(t, "Mac Mini");
    chip_t up = chip(t);
    lv_obj_align(up.bg, LV_ALIGN_TOP_RIGHT, -PAD, 36);
    s_mac.up_bg = up.bg;
    s_mac.up_lbl = up.lbl;

    /* Left: the most urgent alert, tall, then macOS and the monitor. */
    s_mac.banner = box(t, PAD, TOP, 176, 122, C_CRIT_TINT, 12);
    lv_obj_set_style_border_width(s_mac.banner, 2, 0);
    s_mac.bk = text(s_mac.banner, &jolly_font_12, C_CRIT_INK, "");
    lv_obj_set_pos(s_mac.bk, 10, 12);
    s_mac.bv = text(s_mac.banner, &jolly_font_20, C_TEXT, "");
    lv_obj_set_width(s_mac.bv, 156);
    lv_label_set_long_mode(s_mac.bv, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_pos(s_mac.bv, 10, 32);
    s_mac.bd = text(s_mac.banner, &jolly_font_12, C_MUTED, "");
    lv_obj_set_pos(s_mac.bd, 10, 96);

    lv_obj_t *c = card(t, PAD, TOP + 130, 176, 44);
    lv_obj_set_pos(text(c, &jolly_font_12, C_MUTED, "macOS"), 10, 5);
    s_mac.os = text(c, &jolly_font_16, C_TEXT, "—");
    lv_obj_set_pos(s_mac.os, 10, 21);
    c = card(t, PAD, TOP + 182, 176, 44);
    lv_obj_set_pos(text(c, &jolly_font_12, C_MUTED, "Monitor"), 10, 5);
    s_mac.mon = text(c, &jolly_font_16, C_TEXT, "—");
    lv_obj_set_pos(s_mac.mon, 10, 21);

    /* Right: four big numbers, 2x2. */
    const int32_t x0 = PAD + 176 + 8, cw = (W - PAD - x0 - 8) / 2, ch = (BOTTOM - TOP - 8) / 2;
    static const char *const LBL[] = { "CPU", "Temperatura", "Memoria", "Disco" };
    lv_obj_t *cards[4];
    for (int i = 0; i < 4; i++) {
        cards[i] = card(t, x0 + (i % 2) * (cw + 8), TOP + (i / 2) * (ch + 8), cw, ch);
        lv_obj_set_pos(text(cards[i], &jolly_font_12, C_MUTED, LBL[i]), 10, 8);
    }
    s_mac.cpu = big(cards[0], 10, 34, "%");
    s_mac.cpu_meter = meter(cards[0], 10, ch - 18, cw - 20);
    s_mac.temp = big(cards[1], 10, 34, "°C");
    s_mac.therm = text(cards[1], &jolly_font_12, C_OK, "");
    lv_obj_set_pos(s_mac.therm, 10, ch - 22);
    s_mac.mem = text(cards[2], &jolly_font_20, C_TEXT, "—");
    lv_obj_set_pos(s_mac.mem, 10, 38);
    s_mac.swap = text(cards[2], &jolly_font_12, C_MUTED, "");
    lv_obj_set_pos(s_mac.swap, 10, ch - 22);
    s_mac.disk = big(cards[3], 10, 34, "%");
    s_mac.free_gb = text(cards[3], &jolly_font_12, C_MUTED, "");
    lv_obj_set_pos(s_mac.free_gb, 10, ch - 22);

    s_mac.foot = text(t, &jolly_font_12, C_MUTED, "");
    lv_obj_set_pos(s_mac.foot, PAD, H - 20);
}

/* "Respaldo externo: hace 10 días" -> "Respaldo externo" over "hace 10 días". */
static void banner_set(const char *k, const char *v, const char *d, uint32_t bg, uint32_t border, uint32_t ink)
{
    lv_obj_set_style_bg_color(s_mac.banner, lv_color_hex(bg), 0);
    lv_obj_set_style_border_color(s_mac.banner, lv_color_hex(border), 0);
    set_color(s_mac.bk, ink);
    set_text(s_mac.bk, k);
    set_text(s_mac.bv, v);
    set_text(s_mac.bd, d);
}

static void update_mac(bool entering)
{
    char s[64], a[32];
    if (!s_have) {
        banner_set("Guardián", "Sin datos", "", C_STALE_TINT, C_STALE, C_STALE);
        return;
    }
    const jolly_glance_t *g = &s_g;
    snprintf(s, sizeof(s), "ARRIBA · %d d", (int)(g->mac.up_s / 86400));
    set_text(s_mac.up_lbl, s);
    lv_obj_set_style_bg_color(s_mac.up_bg, lv_color_hex(C_OK_TINT), 0);
    set_color(s_mac.up_lbl, C_OK);

    if (g->crit > 0 && g->head[0]) {
        char k[JOLLY_HEAD_MAX + 1];
        strlcpy(k, g->head, sizeof(k));
        char *colon = strstr(k, ": ");
        const char *v = colon ? colon + 2 : "";
        if (colon) {
            *colon = '\0';
        }
        if (g->crit > 1) {
            snprintf(a, sizeof(a), "y %d más", (int)g->crit - 1);
        } else {
            a[0] = '\0';
        }
        banner_set(colon ? k : "Alerta", colon ? v : k, a, C_CRIT_TINT, C_CRIT, C_CRIT_INK);
    } else if (g->warn > 0) {
        snprintf(s, sizeof(s), "%d %s", (int)g->warn, g->warn == 1 ? "aviso" : "avisos");
        banner_set("Por revisar", s, g->head, C_WARN_TINT, C_WARN, C_WARN);
    } else {
        banner_set("Todo en orden", "Sin alertas", "", C_OK_TINT, C_OK, C_OK);
    }

    if (g->mac.updates != JOLLY_NONE && g->mac.updates > 0) {
        snprintf(s, sizeof(s), "%.15s · %d act.", g->mac.macos, (int)g->mac.updates);   /* macos is 16 bytes */
    } else {
        snprintf(s, sizeof(s), "%.15s", g->mac.macos);
    }
    set_text(s_mac.os, s);
    jolly_fmt_ago(g->mac.monitor_age_s, a, sizeof(a));
    snprintf(s, sizeof(s), "%s · %s",
             g->mac.monitor_state == JS_OK ? "vivo" : g->mac.monitor_state == JS_UNKNOWN ? "sin datos" : "parado", a);
    set_text(s_mac.mon, s);
    set_color(s_mac.mon, g->mac.monitor_state == JS_OK ? C_TEXT : C_WARN);

    if (entering || s_mac.cpu.value != g->mac.cpu) {
        big_set(&s_mac.cpu, g->mac.cpu, entering);
        if (entering) {
            lv_bar_set_value(s_mac.cpu_meter, 0, LV_ANIM_OFF);
        }
        lv_bar_set_value(s_mac.cpu_meter, g->mac.cpu, LV_ANIM_ON);
    }
    if (entering || s_mac.temp.value != g->mac.temp) {
        big_set(&s_mac.temp, g->mac.temp, entering);
    }
    set_text(s_mac.therm, jolly_word_therm(g->mac.therm));
    set_color(s_mac.therm, g->mac.therm == JT_NORMAL ? C_OK : g->mac.therm == JT_ALTO ? C_WARN : C_CRIT_INK);
    set_text(s_mac.mem, jolly_word_level(g->mac.mem));
    set_color(s_mac.mem, g->mac.mem == JLV_OK ? C_TEXT : g->mac.mem == JLV_WARN ? C_WARN : C_CRIT_INK);
    char used[12], total[12];
    jolly_fmt_gib(g->mac.swap_mb, used, sizeof(used));
    jolly_fmt_gib(g->mac.swap_tot_mb, total, sizeof(total));
    snprintf(s, sizeof(s), "swap %s/%s GB", used, total);
    set_text(s_mac.swap, s);
    bool swap_high = g->mac.swap_tot_mb > 0 && g->mac.swap_mb * 4 >= g->mac.swap_tot_mb * 3;   /* 75 % */
    set_color(s_mac.swap, swap_high ? C_WARN : C_MUTED);
    if (entering || s_mac.disk.value != g->mac.disk_pct) {
        big_set(&s_mac.disk, g->mac.disk_pct, entering);
    }
    snprintf(s, sizeof(s), "%d GB libres", (int)g->mac.disk_free_gb);
    set_text(s_mac.free_gb, s);
}

/* ---- Page 2: Apps ---- */

EXT_RAM_BSS_ATTR static struct {
    lv_obj_t *down, *down_id, *down_sub, *count_bg, *count_lbl, *empty;
    lv_obj_t *pill[APP_PILLS], *dot[APP_PILLS], *name[APP_PILLS], *port[APP_PILLS];
    char last_down[JOLLY_ID_MAX + 1];
} s_ap;

static void build_apps(lv_obj_t *t)
{
    status_bar(t, &s_chips[2]);
    title(t, "Apps");
    s_ap.down = box(t, 82, 30, 270, 40, C_CRIT_TINT, 10);
    lv_obj_set_style_border_width(s_ap.down, 2, 0);
    lv_obj_set_style_border_color(s_ap.down, lv_color_hex(C_CRIT), 0);
    s_ap.down_id = text(s_ap.down, &jolly_font_16, C_TEXT, "");
    one_line(s_ap.down_id, 250);
    lv_obj_set_pos(s_ap.down_id, 10, 3);
    s_ap.down_sub = text(s_ap.down, &jolly_font_12, C_CRIT_INK, "");
    lv_obj_set_pos(s_ap.down_sub, 10, 21);
    lv_obj_add_flag(s_ap.down, LV_OBJ_FLAG_HIDDEN);
    chip_t c = chip(t);
    lv_obj_align(c.bg, LV_ALIGN_TOP_RIGHT, -PAD, 39);
    s_ap.count_bg = c.bg;
    s_ap.count_lbl = c.lbl;
    s_ap.empty = text(t, &jolly_font_16, C_MUTED, "Cargando apps…");
    lv_obj_set_pos(s_ap.empty, PAD, 90);
    for (int i = 0; i < APP_PILLS; i++) {
        lv_obj_t *p = box(t, 0, 0, 10, 22, C_CARD, 6);
        s_ap.pill[i] = p;
        s_ap.dot[i] = box(p, 5, 7, 8, 8, C_OK, LV_RADIUS_CIRCLE);
        s_ap.name[i] = text(p, &jolly_font_12, C_TEXT, "");
        one_line(s_ap.name[i], 60);
        s_ap.port[i] = text(p, &jolly_font_12, C_MUTED, "");
        lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    }
}

static void shake_cb(void *var, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)var, 82 + v);
}

/* A newly down app: the red strip shakes three times, once. */
static void shake(lv_obj_t *o)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, shake_cb);
    lv_anim_set_values(&a, -5, 5);
    lv_anim_set_duration(&a, 90);
    lv_anim_set_reverse_duration(&a, 90);
    lv_anim_set_repeat_count(&a, 3);
    lv_anim_set_completed_cb(&a, NULL);
    lv_anim_start(&a);
}

static void update_apps(void)
{
    char s[64], a[24];
    if (!s_have) {
        return;
    }
    const jolly_glance_t *g = &s_g;
    snprintf(s, sizeof(s), "%d/%d arriba", (int)g->apps.up, (int)g->apps.total);
    set_text(s_ap.count_lbl, s);
    bool any_down = g->apps.ndown > 0;
    uint32_t bg, fg;
    tone_colors(any_down ? JTONE_WARN : JTONE_OK, &bg, &fg);
    lv_obj_set_style_bg_color(s_ap.count_bg, lv_color_hex(bg), 0);
    set_color(s_ap.count_lbl, fg);
    lv_obj_set_flag(s_ap.down, LV_OBJ_FLAG_HIDDEN, !any_down);
    if (any_down) {
        const jolly_app_down_t *d = &g->apps.down[0];
        set_text(s_ap.down_id, d->id);
        jolly_fmt_ago(d->for_s, a, sizeof(a));
        snprintf(s, sizeof(s), "caída %s · :%d", a, (int)d->port);
        set_text(s_ap.down_sub, s);
        if (strcmp(s_ap.last_down, d->id) != 0) {
            strlcpy(s_ap.last_down, d->id, sizeof(s_ap.last_down));
            shake(s_ap.down);
        }
    } else {
        s_ap.last_down[0] = '\0';
    }

    bool have_list = jolly_client_apps(&s_apps);
    lv_obj_set_flag(s_ap.empty, LV_OBJ_FLAG_HIDDEN, have_list);
    int n = have_list ? s_apps.n : 0;
    int cols = n > 27 ? 4 : 3, rows = n ? (n + cols - 1) / cols : 1;
    int32_t top = 78, gap = 2;
    int32_t ph = (BOTTOM - top - (rows - 1) * gap) / rows;
    ph = ph > 22 ? 22 : ph;
    int32_t pw = (W - 2 * PAD - (cols - 1) * 4) / cols;
    for (int i = 0; i < APP_PILLS; i++) {
        lv_obj_t *p = s_ap.pill[i];
        if (i >= n) {
            lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        int col = i / rows, row = i % rows;   /* down the columns, in port order */
        lv_obj_set_pos(p, PAD + col * (pw + 4), top + row * (ph + gap));
        lv_obj_set_size(p, pw, ph);
        lv_obj_remove_flag(p, LV_OBJ_FLAG_HIDDEN);
        jolly_app_st_t st = s_apps.apps[i].st;
        lv_obj_set_style_opa(p, st == JA_DEP ? LV_OPA_50 : LV_OPA_COVER, 0);
        uint32_t dc = st == JA_UP ? C_OK : st == JA_DOWN ? C_CRIT : C_STALE;
        lv_obj_set_style_bg_color(s_ap.dot[i], lv_color_hex(dc), 0);
        lv_obj_set_flag(s_ap.dot[i], LV_OBJ_FLAG_HIDDEN, st == JA_DEP);
        lv_obj_set_y(s_ap.dot[i], (ph - 8) / 2);
        set_text(s_ap.name[i], s_apps.apps[i].id);
        snprintf(a, sizeof(a), "%d", (int)s_apps.apps[i].port);
        set_text(s_ap.port[i], a);
        lv_obj_align(s_ap.port[i], LV_ALIGN_RIGHT_MID, -5, 0);
        one_line(s_ap.name[i], pw - 18 - 40);
        lv_obj_align(s_ap.name[i], LV_ALIGN_LEFT_MID, 17, 0);
    }
}

/* ---- Page 3: Red ---- */

EXT_RAM_BSS_ATTR static struct {
    lv_obj_t *lat, *dn, *up, *chart, *day, *peak, *speed, *speed_age, *tunnel, *ts;
    lv_chart_series_t *rx, *tx, *drx;
} s_net;

static void build_net(lv_obj_t *t)
{
    status_bar(t, &s_chips[3]);
    title(t, "Red");
    s_net.lat = text(t, &jolly_font_16, C_MUTED, "latencia —");
    lv_obj_align(s_net.lat, LV_ALIGN_TOP_RIGHT, -PAD, 40);

    const int32_t lw = W - 2 * PAD - 8 - 160;
    lv_obj_t *c = card(t, PAD, TOP, lw, BOTTOM - TOP);
    lv_obj_set_pos(text(c, &jolly_font_12, C_MUTED, "Tráfico · últimos 10 min"), 10, 8);
    s_net.dn = text(c, &jolly_font_28, C_TEXT, "↓ —");
    lv_obj_set_pos(s_net.dn, 10, 26);
    s_net.up = text(c, &jolly_font_20, C_ACCENT2, "↑ —");
    lv_obj_set_pos(s_net.up, 150, 32);
    s_net.chart = lv_chart_create(c);
    lv_obj_remove_flag(s_net.chart, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_net.chart, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_pos(s_net.chart, 10, 70);
    lv_obj_set_size(s_net.chart, lw - 20, BOTTOM - TOP - 80);
    lv_chart_set_type(s_net.chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_net.chart, 60);
    lv_chart_set_div_line_count(s_net.chart, 3, 0);
    lv_obj_set_style_bg_opa(s_net.chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_net.chart, 0, 0);
    lv_obj_set_style_pad_all(s_net.chart, 0, 0);
    lv_obj_set_style_line_color(s_net.chart, lv_color_hex(C_LINE), LV_PART_MAIN);
    lv_obj_set_style_size(s_net.chart, 0, 0, LV_PART_INDICATOR);   /* lines, no point markers */
    lv_obj_set_style_line_width(s_net.chart, 2, LV_PART_ITEMS);
    s_net.rx = lv_chart_add_series(s_net.chart, lv_color_hex(C_ACCENT), LV_CHART_AXIS_PRIMARY_Y);
    s_net.tx = lv_chart_add_series(s_net.chart, lv_color_hex(C_ACCENT2), LV_CHART_AXIS_PRIMARY_Y);

    const int32_t x0 = PAD + lw + 8;
    c = card(t, x0, TOP, 160, 86);
    lv_obj_set_pos(text(c, &jolly_font_12, C_MUTED, "24 h · bajada"), 8, 6);
    s_net.peak = text(c, &jolly_font_12, C_MUTED, "");
    lv_obj_align(s_net.peak, LV_ALIGN_TOP_RIGHT, -8, 6);
    s_net.day = lv_chart_create(c);
    lv_obj_remove_flag(s_net.day, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_net.day, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_pos(s_net.day, 8, 26);
    lv_obj_set_size(s_net.day, 144, 52);
    lv_chart_set_type(s_net.day, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(s_net.day, 24);
    lv_chart_set_div_line_count(s_net.day, 0, 0);
    lv_obj_set_style_bg_opa(s_net.day, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_net.day, 0, 0);
    lv_obj_set_style_pad_all(s_net.day, 0, 0);
    lv_obj_set_style_pad_column(s_net.day, 1, 0);
    s_net.drx = lv_chart_add_series(s_net.day, lv_color_hex(C_ACCENT), LV_CHART_AXIS_PRIMARY_Y);

    c = card(t, x0, TOP + 94, 160, 56);
    lv_obj_set_pos(text(c, &jolly_font_12, C_MUTED, "Velocidad"), 8, 6);
    s_net.speed_age = text(c, &jolly_font_12, C_MUTED, "");
    lv_obj_align(s_net.speed_age, LV_ALIGN_TOP_RIGHT, -8, 6);
    s_net.speed = text(c, &jolly_font_16, C_TEXT, "—");
    lv_obj_set_pos(s_net.speed, 8, 26);

    c = card(t, x0, TOP + 158, 160, 30);
    lv_obj_set_pos(text(c, &jolly_font_12, C_MUTED, "Túnel"), 8, 8);
    s_net.tunnel = text(c, &jolly_font_16, C_TEXT, "—");
    lv_obj_align(s_net.tunnel, LV_ALIGN_RIGHT_MID, -8, 0);
    c = card(t, x0, TOP + 196, 160, 30);
    lv_obj_set_pos(text(c, &jolly_font_12, C_MUTED, "Tailscale"), 8, 8);
    s_net.ts = text(c, &jolly_font_16, C_TEXT, "—");
    lv_obj_align(s_net.ts, LV_ALIGN_RIGHT_MID, -8, 0);
}

static int32_t series_max(const int32_t *v, int n)
{
    int32_t m = 1;
    for (int i = 0; i < n; i++) {
        m = v[i] > m ? v[i] : m;
    }
    return m;
}

static void chart_fill(lv_obj_t *chart, lv_chart_series_t *ser, const int32_t *v, int n)
{
    for (int i = 0; i < n; i++) {
        lv_chart_set_series_value_by_id(chart, ser, (uint32_t)i, v[i] < 0 ? LV_CHART_POINT_NONE : v[i]);
    }
}

static void update_net(void)
{
    char s[48], a[16], b[16];
    if (!s_have) {
        return;
    }
    const jolly_glance_t *g = &s_g;
    if (g->net.lat_ms == JOLLY_NONE) {
        snprintf(s, sizeof(s), "latencia —");
    } else {
        snprintf(s, sizeof(s), "latencia %d ms", (int)g->net.lat_ms);
    }
    set_text(s_net.lat, s);
    lv_obj_align(s_net.lat, LV_ALIGN_TOP_RIGHT, -PAD, 40);
    jolly_fmt_tenths(g->net.rx_kbps, a, sizeof(a));
    snprintf(s, sizeof(s), "↓ %s Mb/s", a);
    set_text(s_net.dn, s);
    jolly_fmt_tenths(g->net.tx_kbps, a, sizeof(a));
    snprintf(s, sizeof(s), "↑ %s", a);
    set_text(s_net.up, s);
    lv_obj_align_to(s_net.up, s_net.dn, LV_ALIGN_OUT_RIGHT_BOTTOM, 14, -2);
    if (g->net.has_speed) {
        snprintf(s, sizeof(s), "%d↓  %d↑ Mbps", (int)g->net.speed_down, (int)g->net.speed_up);
        jolly_fmt_ago(g->net.speed_age_s, a, sizeof(a));
    } else {
        snprintf(s, sizeof(s), "—");
        a[0] = '\0';
    }
    set_text(s_net.speed, s);
    set_text(s_net.speed_age, a);
    lv_obj_align(s_net.speed_age, LV_ALIGN_TOP_RIGHT, -8, 6);
    snprintf(s, sizeof(s), "%d/%d", (int)g->net.cf, (int)g->net.cf_exp);
    set_text(s_net.tunnel, s);
    set_color(s_net.tunnel, g->net.cf >= g->net.cf_exp ? C_OK : g->net.cf ? C_WARN : C_CRIT_INK);
    lv_obj_align(s_net.tunnel, LV_ALIGN_RIGHT_MID, -8, 0);
    snprintf(s, sizeof(s), "%d nodos", (int)g->net.ts_online);
    set_text(s_net.ts, s);
    set_color(s_net.ts, !strcmp(g->net.ts_state, "up") ? C_TEXT : C_WARN);
    lv_obj_align(s_net.ts, LV_ALIGN_RIGHT_MID, -8, 0);

    if (jolly_client_series(false, &s_s10)) {
        int32_t m = series_max(s_s10.rx, 60), mt = series_max(s_s10.tx, 60);
        m = (m > mt ? m : mt) * 11 / 10;
        lv_chart_set_axis_range(s_net.chart, LV_CHART_AXIS_PRIMARY_Y, 0, m);
        chart_fill(s_net.chart, s_net.rx, s_s10.rx, 60);
        chart_fill(s_net.chart, s_net.tx, s_s10.tx, 60);
        lv_chart_refresh(s_net.chart);
    }
    if (jolly_client_series(true, &s_s24)) {
        lv_chart_set_axis_range(s_net.day, LV_CHART_AXIS_PRIMARY_Y, 0, series_max(s_s24.rx, 24));
        chart_fill(s_net.day, s_net.drx, s_s24.rx, 24);
        lv_chart_refresh(s_net.day);
        (void)b;
        snprintf(s, sizeof(s), "pico %d", (int)((s_s24.rx_peak_kbps + 500) / 1000));   /* Mb/s */
        set_text(s_net.peak, s);
        lv_obj_align(s_net.peak, LV_ALIGN_TOP_RIGHT, -8, 6);
    }
}

/* ---- Page 4: Seguridad ---- */

EXT_RAM_BSS_ATTR static struct {
    lv_obj_t *hd, *shield, *sym, *hb, *hs;
    big_t ports, plists, nodes;
    lv_obj_t *unreg, *ssh;
    lv_obj_t *row[EVENT_ROWS], *stripe[EVENT_ROWS], *txt[EVENT_ROWS], *sub[EVENT_ROWS], *none;
} s_sec;

static void build_sec(lv_obj_t *t)
{
    status_bar(t, &s_chips[4]);
    const int32_t lw = 188;
    s_sec.hd = box(t, PAD, 34, lw, 60, C_OK_TINT, 12);
    lv_obj_set_style_border_width(s_sec.hd, 2, 0);
    s_sec.shield = box(s_sec.hd, 8, 8, 40, 40, C_OK, 14);
    s_sec.sym = text(s_sec.shield, &lv_font_montserrat_20, C_BG, LV_SYMBOL_OK);
    lv_obj_center(s_sec.sym);
    s_sec.hb = text(s_sec.hd, &jolly_font_16, C_TEXT, "");
    one_line(s_sec.hb, lw - 64);
    lv_obj_set_pos(s_sec.hb, 56, 10);
    s_sec.hs = text(s_sec.hd, &jolly_font_12, C_MUTED, "");
    one_line(s_sec.hs, lw - 64);
    lv_obj_set_pos(s_sec.hs, 56, 32);

    /* Narrow cards: short names, the detail underneath. */
    static const char *const LBL[] = { "Puertos", "Inicio", "Nodos" };
    static const char *const SUB[] = { "", "servicios", "Tailscale" };
    const int32_t cw = (lw - 12) / 3;
    big_t *bigs[] = { &s_sec.ports, &s_sec.plists, &s_sec.nodes };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *c = card(t, PAD + i * (cw + 6), 102, cw, 84);
        lv_obj_set_pos(text(c, &jolly_font_12, C_MUTED, LBL[i]), 6, 6);
        *bigs[i] = big(c, 6, 24, "");
        lv_obj_t *sub = text(c, &jolly_font_12, C_MUTED, SUB[i]);
        lv_obj_set_pos(sub, 6, 62);
        if (i == 0) {
            s_sec.unreg = sub;
        }
    }
    lv_obj_t *c = card(t, PAD, 194, lw, 36);
    lv_obj_set_pos(text(c, &jolly_font_12, C_MUTED, "SSH · 24 h"), 8, 10);
    s_sec.ssh = text(c, &jolly_font_16, C_TEXT, "—");
    lv_obj_align(s_sec.ssh, LV_ALIGN_RIGHT_MID, -8, 0);

    const int32_t x0 = PAD + lw + 8, rw = W - PAD - x0;
    lv_obj_set_pos(text(t, &jolly_font_12, C_MUTED, "Recientes"), x0, 36);
    for (int i = 0; i < EVENT_ROWS; i++) {
        lv_obj_t *r = card(t, x0, 54 + i * 50, rw, 44);
        lv_obj_set_style_radius(r, 10, 0);
        s_sec.row[i] = r;
        s_sec.stripe[i] = box(r, 0, 0, 4, 44, C_WARN, 0);
        s_sec.txt[i] = text(r, &jolly_font_12, C_TEXT, "");
        one_line(s_sec.txt[i], rw - 18);
        lv_obj_set_pos(s_sec.txt[i], 12, 6);
        s_sec.sub[i] = text(r, &jolly_font_12, C_MUTED, "");
        lv_obj_set_pos(s_sec.sub[i], 12, 24);
        lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN);
    }
    s_sec.none = text(t, &jolly_font_12, C_MUTED, "Sin novedades");
    lv_obj_set_pos(s_sec.none, x0, 58);
}

static bool security_cat(const char *cat)
{
    return !strcmp(cat, "port") || !strcmp(cat, "plist") || !strcmp(cat, "tsnode") || !strcmp(cat, "ssh");
}

static void update_sec(bool entering)
{
    char s[64], a[24];
    if (!s_have) {
        return;
    }
    const jolly_glance_t *g = &s_g;
    jolly_level_t lv = g->sec.level;
    uint32_t tint = lv == JLV_CRIT ? C_CRIT_TINT : lv == JLV_WARN ? C_WARN_TINT : C_OK_TINT;
    uint32_t col = lv == JLV_CRIT ? C_CRIT : lv == JLV_WARN ? C_WARN : C_OK;
    lv_obj_set_style_bg_color(s_sec.hd, lv_color_hex(tint), 0);
    lv_obj_set_style_border_color(s_sec.hd, lv_color_hex(col), 0);
    lv_obj_set_style_bg_color(s_sec.shield, lv_color_hex(col), 0);
    set_text(s_sec.sym, lv == JLV_OK ? LV_SYMBOL_OK : LV_SYMBOL_WARNING);
    set_text(s_sec.hb, lv == JLV_CRIT ? "Alerta de seguridad" : lv == JLV_WARN ? "Por revisar" : "Todo en orden");
    if (g->sec.learning) {
        snprintf(s, sizeof(s), "aprendiendo lo normal");
    } else if (g->sec.ports_unreg > 0) {
        snprintf(s, sizeof(s), "%d %s sin registro", (int)g->sec.ports_unreg,
                 g->sec.ports_unreg == 1 ? "puerto" : "puertos");
    } else {
        snprintf(s, sizeof(s), "sin novedades");
    }
    set_text(s_sec.hs, s);

    if (entering || s_sec.ports.value != g->sec.ports) {
        big_set(&s_sec.ports, g->sec.ports, entering);
    }
    if (entering || s_sec.plists.value != g->sec.plists) {
        big_set(&s_sec.plists, g->sec.plists, entering);
    }
    if (entering || s_sec.nodes.value != g->sec.ts_nodes) {
        big_set(&s_sec.nodes, g->sec.ts_nodes, entering);
    }
    snprintf(s, sizeof(s), "%d sin reg.", (int)g->sec.ports_unreg);
    set_text(s_sec.unreg, s);
    set_color(s_sec.unreg, g->sec.ports_unreg ? C_WARN : C_MUTED);
    snprintf(s, sizeof(s), "%d %s", (int)g->sec.ssh_fail_24h, g->sec.ssh_fail_24h == 1 ? "fallo" : "fallos");
    set_text(s_sec.ssh, s);
    set_color(s_sec.ssh, g->sec.ssh_fail_24h ? C_WARN : C_OK);
    lv_obj_align(s_sec.ssh, LV_ALIGN_RIGHT_MID, -8, 0);

    /* The last four security events, newest first, each with its stripe and
     * its word, so they read without colour. Ages are the Guardián's clock. */
    int shown = 0;
    if (jolly_client_events(&s_events)) {
        for (int i = 0; i < s_events.n && shown < EVENT_ROWS; i++) {
            if (!security_cat(s_events.ev[i].cat)) {
                continue;
            }
            jolly_level_t sev = s_events.ev[i].sev;
            lv_obj_set_style_bg_color(s_sec.stripe[shown],
                                      lv_color_hex(sev == JLV_CRIT ? C_CRIT : sev == JLV_WARN ? C_WARN : C_ACCENT), 0);
            set_text(s_sec.txt[shown], s_events.ev[i].txt);
            jolly_fmt_ago(s_events.ts - s_events.ev[i].t, a, sizeof(a));
            snprintf(s, sizeof(s), "%s · %s", a, jolly_word_sev(sev));
            set_text(s_sec.sub[shown], s);
            lv_obj_remove_flag(s_sec.row[shown], LV_OBJ_FLAG_HIDDEN);
            shown++;
        }
    }
    for (int i = shown; i < EVENT_ROWS; i++) {
        lv_obj_add_flag(s_sec.row[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_flag(s_sec.none, LV_OBJ_FLAG_HIDDEN, shown > 0);
}

/* ---- API ---- */

void jolly_pages_create(lv_obj_t *tv, int first_col)
{
    s_first_col = first_col;
    build_mac(page_tile(tv, first_col));
    build_apps(page_tile(tv, first_col + 1));
    build_net(page_tile(tv, first_col + 2));
    build_sec(page_tile(tv, first_col + 3));
}

void jolly_pages_face(lv_obj_t *face)
{
    s_chips[0] = chip(face);
    lv_obj_align(s_chips[0].bg, LV_ALIGN_TOP_RIGHT, -PAD, 3);
}

static void update_page(bool entering)
{
    switch (s_page) {
    case 1:
        update_mac(entering);
        break;
    case 2:
        update_apps();
        break;
    case 3:
        update_net();
        break;
    case 4:
        update_sec(entering);
        break;
    default:
        break;
    }
}

void jolly_pages_show(int page)
{
    static const unsigned WANT[JOLLY_PAGES + 1] = {
        0, 0, JOLLY_WANT_APPS, JOLLY_WANT_SERIES_10M | JOLLY_WANT_SERIES_24H, JOLLY_WANT_EVENTS,
    };
    s_page = page >= 1 && page <= JOLLY_PAGES ? page : 0;
    jolly_client_want(WANT[s_page]);
    if (s_page) {
        s_have = jolly_client_snapshot(&s_g, &s_link);
        update_page(true);   /* numbers count up, meters fill */
        s_next_refresh = s_now + REFRESH_MS;
    }
}

void jolly_pages_tick(int64_t now_ms)
{
    s_now = now_ms;
    if (now_ms < s_next_refresh) {
        return;
    }
    s_next_refresh = now_ms + REFRESH_MS;
    s_have = jolly_client_snapshot(&s_g, &s_link);
    char s[48];
    jolly_tone_t tone;
    jolly_fmt_chip(s_link.link, s_link.auth_status, s_have ? &s_g : NULL, s, sizeof(s), &tone);
    for (int i = 0; i <= JOLLY_PAGES; i++) {
        if (s_chips[i].bg) {
            chip_set(&s_chips[i], s, tone);
        }
    }
    if (s_page == 1 && s_link.have_ok) {
        char a[24];
        jolly_fmt_ago((now_ms - s_link.last_ok_ms) / 1000, a, sizeof(a));
        snprintf(s, sizeof(s), "actualizado %s", a);
        set_text(s_mac.foot, s);
    }
    if (s_page) {
        update_page(false);
    }
}
