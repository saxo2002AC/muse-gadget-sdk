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

#include "jolly/jolly_eyes.h"
#include "jolly/jolly_face_spec.h"
#include "jolly/jolly_gesture.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#if __has_include("esp_attr.h")
#include "esp_attr.h"
#endif
#ifndef EXT_RAM_BSS_ATTR
#define EXT_RAM_BSS_ATTR
#endif

LV_FONT_DECLARE(jolly_font_20)
LV_FONT_DECLARE(jolly_font_28)

/* The mockup's 480x320 screen; the box starts below the status line, so a
 * mockup y is box y + MY0. */
#define MY0 30
#define BOX_W 480
#define BOX_H 286
#define Y(v) ((v) - MY0)

#define EYE_W 112
#define EYE_H 128
#define EYE_R 30
#define EYE_RIM 3
#define EYE_Y 150
static const int16_t EYE_X[2] = { 170, 310 };
#define BROW_W 84
#define BROW_T 9
#define BROW_Y 64
#define BLUSH_Y 228
static const int16_t BLUSH_X[2] = { 108, 372 };
#define MOUTH_X 240
#define MOUTH_Y 254
#define LINE_BOX_W 140             /* a line mouth's box: fixed, so a change redraws only it */
#define LINE_BOX_H 70

/* The UI's background is black: lids and the core dot are holes onto it. */
#define BG 0x000000
#define MOUTH_BG 0x070816
#define DROP_COLOR 0x7FD4FF
#define PUFF_COLOR 0x9FDBFF
#define ZZZ_COLOR 0xB9A6FF
#define ALARM_COLOR 0xFF5C6C

#define HEARTS 6
#define HEART_W 20
#define HEART_H 18
#define HEART_MS 1700
#define HEART_RISE 150
#define HEART_EVERY_MS 600
#define COLOR_MS 300               /* the mood's colour fades in */
#define GLOW_STILL_MS 300          /* an eye still this long gets its glow back */
#define FLASH_MS 260
#define STEP_MS (1000.0f / 60)     /* the mockup's springs are per 60 Hz frame */
#define BREATH_MS 3200

/* The springs: the mockup's BASE keys. */
enum { P_ESX, P_ESY, P_SPR, P_GOX, P_GOY, P_LTL, P_LTR, P_LRL, P_LRR, P_LBL, P_LBR, P_LHL, P_LHR, P_CORE,
       P_BYL, P_BYR, P_BRL, P_BRR, P_BLUSH, P_OS, P_MX, P_MY, P_COUNT };

typedef struct {
    lv_obj_t *shade, *body, *spec, *core, *lid_t, *lid_b, *lid_h, *arc_up, *arc_dn, *spiral[3];
    lv_point_precise_t lid_pts[2];
    int32_t x, y, w, h;            /* the body last frame: a change is movement */
    int64_t still_since;
} eye_t;

typedef struct {
    lv_obj_t *img;
    int64_t t0;                    /* 0: free */
    int32_t x, y, drift;
} heart_t;

typedef struct {
    lv_obj_t *line;
    lv_point_precise_t pts[2];
} bar_t;

/* Widgets live in LVGL's heap (PSRAM); this state, the points the lines
 * keep pointers to, and the heart's mask are in PSRAM too. */
EXT_RAM_BSS_ATTR static struct {
    eye_t eye[2];
    bar_t brow[2];
    bar_t blush[6];
    lv_obj_t *mline, *pill, *tongue[3], *ring, *grill, *teeth[6], *gtongue;
    lv_point_precise_t mpts[JOLLY_MOUTH_POINTS], mkeep[JOLLY_MOUTH_POINTS];
    int mn;
    heart_t hearts[HEARTS];
    lv_obj_t *zzz[3], *drop, *puff[3], *flash, *alarm[4];
    uint8_t heart_a8[HEART_W * HEART_H];
    lv_image_dsc_t heart_dsc;
} s;

static lv_obj_t *s_box;
static void (*s_on_pet)(void);
static void (*s_on_hold)(bool down);
static jolly_gesture_t s_gesture;

static jolly_mood_t s_mood = JM_TRANQUILO, s_pending = JM_TRANQUILO, s_forced = JM_COUNT;
static const jolly_face_mood_t *s_f;
static float s_x[P_COUNT], s_v[P_COUNT], s_t[P_COUNT];
static struct { float x, y, vx, vy, tx, ty; } s_g;
static struct { float lvl; bool round; int64_t next; } s_talk;
static float s_level;
static uint32_t s_color_from, s_color_to;
static int64_t s_now, s_last, s_color_t0, s_flash_t0, s_next_saccade, s_next_blink, s_next_heart, s_t0;
static float s_step_acc;
/* A blink: started at s_blink_t0, lasting s_blink_ms; a double queues a second. */
static int64_t s_blink_t0, s_blink_again;
static int s_blink_ms;

uint32_t jolly_mood_color(jolly_mood_t m)
{
    return jolly_face_mood(m)->color;
}

static int32_t rnd(int32_t lo, int32_t hi)
{
    return hi > lo ? (int32_t)lv_rand(lo, hi) : lo;
}

static float frnd(float lo, float hi)
{
    return lo + (hi - lo) * (float)lv_rand(0, 1000) / 1000.0f;
}

/* ---- Setters that touch LVGL only on a change: a style set redraws even
 * when the value is the same, and so does hiding a hidden object. ---- */

static void show(lv_obj_t *o, bool on)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) == on) {
        lv_obj_set_flag(o, LV_OBJ_FLAG_HIDDEN, !on);
    }
}

static void bg_color(lv_obj_t *o, lv_color_t c)
{
    if (!lv_color_eq(lv_obj_get_style_bg_color(o, 0), c)) {
        lv_obj_set_style_bg_color(o, c, 0);
    }
}

static void bg_grad(lv_obj_t *o, lv_color_t c)
{
    if (!lv_color_eq(lv_obj_get_style_bg_grad_color(o, 0), c)) {
        lv_obj_set_style_bg_grad_color(o, c, 0);
    }
}

static void bg_opa(lv_obj_t *o, lv_opa_t a)
{
    if (lv_obj_get_style_bg_opa(o, 0) != a) {
        lv_obj_set_style_bg_opa(o, a, 0);
    }
}

static void border_color(lv_obj_t *o, lv_color_t c)
{
    if (!lv_color_eq(lv_obj_get_style_border_color(o, 0), c)) {
        lv_obj_set_style_border_color(o, c, 0);
    }
}

static void border_opa(lv_obj_t *o, lv_opa_t a)
{
    if (lv_obj_get_style_border_opa(o, 0) != a) {
        lv_obj_set_style_border_opa(o, a, 0);
    }
}

static void radius(lv_obj_t *o, int32_t r)
{
    if (lv_obj_get_style_radius(o, 0) != r) {
        lv_obj_set_style_radius(o, r, 0);
    }
}

static void line_color(lv_obj_t *o, lv_color_t c)
{
    if (!lv_color_eq(lv_obj_get_style_line_color(o, 0), c)) {
        lv_obj_set_style_line_color(o, c, 0);
    }
}

static void line_opa(lv_obj_t *o, lv_opa_t a)
{
    if (lv_obj_get_style_line_opa(o, 0) != a) {
        lv_obj_set_style_line_opa(o, a, 0);
    }
}

static void line_width(lv_obj_t *o, int32_t w)
{
    if (lv_obj_get_style_line_width(o, 0) != w) {
        lv_obj_set_style_line_width(o, w, 0);
    }
}

static void arc_color(lv_obj_t *o, lv_color_t c)
{
    if (!lv_color_eq(lv_obj_get_style_arc_color(o, LV_PART_INDICATOR), c)) {
        lv_obj_set_style_arc_color(o, c, LV_PART_INDICATOR);
    }
}

static void shadow(lv_obj_t *o, int32_t w, lv_color_t c)
{
    if (lv_obj_get_style_shadow_width(o, 0) != w) {
        lv_obj_set_style_shadow_width(o, w, 0);
    }
    if (w && !lv_color_eq(lv_obj_get_style_shadow_color(o, 0), c)) {
        lv_obj_set_style_shadow_color(o, c, 0);
    }
}

static void obj_opa(lv_obj_t *o, lv_opa_t a)
{
    if (lv_obj_get_style_opa(o, 0) != a) {
        lv_obj_set_style_opa(o, a, 0);
    }
}

static void place(lv_obj_t *o, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_set_pos(o, x, y);       /* both no-ops when nothing changed */
    lv_obj_set_size(o, w, h);
}

/* A line's points, set only when they changed (lv_line keeps the pointer). */
static void line_points(lv_obj_t *o, lv_point_precise_t *keep, const lv_point_precise_t *pts, int n)
{
    if (memcmp(keep, pts, sizeof(*pts) * n) || lv_line_get_point_count(o) != (uint32_t)n) {
        memcpy(keep, pts, sizeof(*pts) * n);
        lv_line_set_points(o, keep, n);
    }
}

static void arc_angles(lv_obj_t *o, int32_t rot, int32_t start, int32_t end)
{
    if (lv_arc_get_rotation(o) != rot) {
        lv_arc_set_rotation(o, rot);
    }
    if (lv_arc_get_angle_start(o) != start || lv_arc_get_angle_end(o) != end) {
        lv_arc_set_angles(o, start, end);
    }
}

static int32_t ri(float v)
{
    return (int32_t)lroundf(v);
}

static lv_color_t mix(uint32_t a, uint32_t b, uint8_t amount_of_a)
{
    return lv_color_mix(lv_color_hex(a), lv_color_hex(b), amount_of_a);
}

/* ---- Building ---- */

/* A heart's alpha mask: the classic implicit curve, (x^2 + y^2 - 1)^3 <= x^2 y^3. */
static void make_heart(void)
{
    for (int py = 0; py < HEART_H; py++) {
        for (int px = 0; px < HEART_W; px++) {
            float x = (px + 0.5f - HEART_W / 2.0f) / (HEART_W / 2.2f);
            float y = (HEART_H / 2.0f - (py + 0.5f)) / (HEART_H / 2.4f) + 0.15f;
            float a = x * x + y * y - 1.0f;
            s.heart_a8[py * HEART_W + px] = a * a * a - x * x * y * y * y <= 0.0f ? 0xFF : 0x00;
        }
    }
    memset(&s.heart_dsc, 0, sizeof(s.heart_dsc));
    s.heart_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s.heart_dsc.header.cf = LV_COLOR_FORMAT_A8;
    s.heart_dsc.header.w = HEART_W;
    s.heart_dsc.header.h = HEART_H;
    s.heart_dsc.header.stride = HEART_W;
    s.heart_dsc.data = s.heart_a8;
    s.heart_dsc.data_size = sizeof(s.heart_a8);
}

static lv_obj_t *plain(lv_obj_t *parent, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    return o;
}

static lv_obj_t *line(lv_obj_t *parent, int32_t width, bool round)
{
    lv_obj_t *o = lv_line_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_line_width(o, width, 0);
    lv_obj_set_style_line_rounded(o, round, 0);
    return o;
}

static lv_obj_t *arc(lv_obj_t *parent, int32_t size, int32_t width)
{
    lv_obj_t *o = lv_arc_create(parent);
    lv_obj_remove_style(o, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_opa(o, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(o, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(o, true, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_size(o, size, size);
    lv_arc_set_bg_angles(o, 0, 360);
    return o;
}

static void make_eye(eye_t *e)
{
    e->shade = plain(s_box, BG);
    lv_obj_set_style_radius(e->shade, LV_RADIUS_CIRCLE, 0);
    e->body = plain(s_box, 0x8BEBFF);
    lv_obj_set_style_radius(e->body, EYE_R, 0);
    lv_obj_set_style_clip_corner(e->body, true, 0);   /* lids and highlight follow the corners */
    lv_obj_set_style_border_width(e->body, EYE_RIM, 0);
    lv_obj_set_style_bg_grad_dir(e->body, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_spread(e->body, -4, 0);
    lv_obj_set_style_shadow_opa(e->body, 115, 0);   /* 45 %, the mockup's glow */
    static const int32_t SP_D[3] = { 76, 50, 24 }, SP_W[3] = { 7, 7, 6 };
    for (int i = 0; i < 3; i++) {
        e->spiral[i] = arc(e->body, SP_D[i], SP_W[i]);
        lv_obj_align(e->spiral[i], LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_flag(e->spiral[i], LV_OBJ_FLAG_HIDDEN);
    }
    e->spec = plain(e->body, 0xFFFFFF);
    lv_obj_set_style_radius(e->spec, 6, 0);
    lv_obj_set_style_bg_opa(e->spec, 199, 0);
    e->core = plain(e->body, BG);
    e->lid_b = plain(e->body, BG);
    e->lid_h = plain(e->body, BG);
    lv_obj_set_style_radius(e->lid_h, LV_RADIUS_CIRCLE, 0);
    e->lid_t = line(e->body, 1, false);
    lv_obj_set_style_line_color(e->lid_t, lv_color_hex(BG), 0);
    e->arc_up = arc(s_box, 92, 9);
    lv_arc_set_angles(e->arc_up, 180, 360);   /* ^: the top of a circle */
    e->arc_dn = arc(s_box, 92, 9);
    lv_arc_set_angles(e->arc_dn, 0, 180);     /* ‿: its bottom */
    for (lv_obj_t **o = (lv_obj_t *[]){ e->core, e->lid_b, e->lid_h, e->lid_t, e->arc_up, e->arc_dn }, **end = o + 6;
         o < end; o++) {
        lv_obj_add_flag(*o, LV_OBJ_FLAG_HIDDEN);
    }
    e->still_since = 0;
}

/* Every touch on the face goes through the gesture classifier: a tap pets,
 * a hold talks, a swipe is the tileview's. */
static void on_box_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    jolly_gesture_sample_t sm = code == LV_EVENT_RELEASED ? JGS_RELEASED
                              : code == LV_EVENT_PRESS_LOST ? JGS_LOST
                                                            : JGS_PRESSED;
    lv_point_t p = { 0, 0 };
    lv_area_t a;
    lv_indev_t *indev = lv_indev_active();
    if (indev) {
        lv_indev_get_point(indev, &p);
    }
    lv_obj_get_coords(s_box, &a);
    switch (jolly_gesture_feed(&s_gesture, sm, p.x - a.x1, p.y - a.y1, (int64_t)lv_tick_get())) {
    case JG_TAP:
        jolly_eyes_pet(s_gesture.x0, s_gesture.y0);
        if (s_on_pet) {
            s_on_pet();
        }
        break;
    case JG_HOLD_START:
        if (s_on_hold) {
            s_on_hold(true);
        }
        break;
    case JG_HOLD_END:
        if (s_on_hold) {
            s_on_hold(false);
        }
        break;
    default:
        break;
    }
}

bool jolly_eyes_holding(void)
{
    return jolly_gesture_holding(&s_gesture);
}

bool jolly_eyes_touched(void)
{
    return s_gesture.state != 0;   /* one word, written by the LVGL task: a stale read is harmless */
}

static void apply_mood(jolly_mood_t m, bool snap);

void jolly_eyes_create(lv_obj_t *parent, int32_t y, void (*on_pet)(void), void (*on_hold)(bool down))
{
    (void)y;
    s_on_pet = on_pet;
    s_on_hold = on_hold;
    jolly_gesture_reset(&s_gesture);
    make_heart();

    /* The alert's pulsing border: four strips at the screen's edges, so a
     * pulse redraws them and not the whole screen. */
    for (int i = 0; i < 4; i++) {
        s.alarm[i] = plain(parent, ALARM_COLOR);
        lv_obj_add_flag(s.alarm[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_size(s.alarm[0], BOX_W, 3);
    lv_obj_align(s.alarm[0], LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_size(s.alarm[1], BOX_W, 3);
    lv_obj_align(s.alarm[1], LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_size(s.alarm[2], 3, LV_PCT(100));
    lv_obj_align(s.alarm[2], LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_size(s.alarm[3], 3, LV_PCT(100));
    lv_obj_align(s.alarm[3], LV_ALIGN_TOP_RIGHT, 0, 0);

    s_box = lv_obj_create(parent);
    lv_obj_remove_style_all(s_box);
    lv_obj_set_size(s_box, BOX_W, BOX_H);
    lv_obj_remove_flag(s_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_box, LV_OBJ_FLAG_CLICKABLE);
    static const lv_event_code_t TOUCH[] = { LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED,
                                             LV_EVENT_PRESS_LOST };
    for (size_t i = 0; i < sizeof(TOUCH) / sizeof(TOUCH[0]); i++) {
        lv_obj_add_event_cb(s_box, on_box_event, TOUCH[i], NULL);
    }
    lv_obj_align(s_box, LV_ALIGN_TOP_LEFT, 0, MY0);

    /* Back to front: blush, eyes, brows, mouth, extras, the flash. */
    for (int i = 0; i < 6; i++) {
        s.blush[i].line = line(s_box, 5, true);
        lv_obj_set_size(s.blush[i].line, 14, 24);
        lv_obj_add_flag(s.blush[i].line, LV_OBJ_FLAG_HIDDEN);
        lv_point_precise_t p[2] = { { 11, 3 }, { 3, 21 } };   /* skewed: "/" */
        line_points(s.blush[i].line, s.blush[i].pts, p, 2);
    }
    for (int i = 0; i < 2; i++) {
        make_eye(&s.eye[i]);
    }
    for (int i = 0; i < 2; i++) {
        s.brow[i].line = line(s_box, BROW_T, true);
        lv_obj_set_size(s.brow[i].line, 100, 60);
    }
    s.mline = line(s_box, 8, true);
    lv_obj_set_size(s.mline, LINE_BOX_W, LINE_BOX_H);
    s.pill = plain(s_box, MOUTH_BG);
    lv_obj_set_style_border_width(s.pill, 7, 0);
    lv_obj_set_style_clip_corner(s.pill, true, 0);
    for (int i = 0; i < 3; i++) {
        s.tongue[i] = plain(s.pill, JOLLY_FACE_TONGUE);
        lv_obj_set_size(s.tongue[i], 10, 10);
    }
    s.ring = plain(s_box, MOUTH_BG);
    lv_obj_set_style_border_width(s.ring, 7, 0);
    lv_obj_set_style_radius(s.ring, LV_RADIUS_CIRCLE, 0);
    s.grill = plain(s_box, MOUTH_BG);
    lv_obj_set_style_border_width(s.grill, 5, 0);
    lv_obj_set_style_radius(s.grill, 10, 0);
    lv_obj_set_size(s.grill, 116, 40);
    for (int i = 0; i < 6; i++) {
        s.teeth[i] = plain(s.grill, 0xFF7A45);
        lv_obj_set_style_radius(s.teeth[i], 3, 0);
        lv_obj_set_pos(s.teeth[i], 4 + i * 17, 4);   /* 6 of 13 px with 4 px gaps in 106 x 30 */
        lv_obj_set_size(s.teeth[i], 13, 22);
    }
    s.gtongue = plain(s_box, JOLLY_FACE_TONGUE);
    lv_obj_set_size(s.gtongue, 20, 20);
    for (lv_obj_t **o = (lv_obj_t *[]){ s.mline, s.pill, s.ring, s.grill, s.gtongue }, **end = o + 5; o < end; o++) {
        lv_obj_add_flag(*o, LV_OBJ_FLAG_HIDDEN);
    }

    for (int i = 0; i < 3; i++) {
        s.puff[i] = plain(s_box, PUFF_COLOR);
        lv_obj_set_style_bg_opa(s.puff[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s.puff[i], 3, 0);
        lv_obj_set_style_border_color(s.puff[i], lv_color_hex(PUFF_COLOR), 0);
        lv_obj_set_style_radius(s.puff[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_add_flag(s.puff[i], LV_OBJ_FLAG_HIDDEN);
    }
    s.drop = plain(s_box, DROP_COLOR);
    lv_obj_set_size(s.drop, 16, 22);
    lv_obj_set_style_radius(s.drop, 8, 0);
    lv_obj_add_flag(s.drop, LV_OBJ_FLAG_HIDDEN);
    static const char *const ZZZ[3] = { "z", "z", "Z" };
    for (int i = 0; i < 3; i++) {
        s.zzz[i] = lv_label_create(s_box);
        lv_label_set_text_static(s.zzz[i], ZZZ[i]);
        lv_obj_set_style_text_font(s.zzz[i], i == 2 ? &jolly_font_28 : &jolly_font_20, 0);
        lv_obj_set_style_text_color(s.zzz[i], lv_color_hex(ZZZ_COLOR), 0);
        lv_obj_add_flag(s.zzz[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < HEARTS; i++) {
        lv_obj_t *h = lv_image_create(s_box);
        lv_image_set_src(h, &s.heart_dsc);
        lv_obj_set_style_image_recolor(h, lv_color_hex(0xFF7FBF), 0);
        lv_obj_set_style_image_recolor_opa(h, LV_OPA_COVER, 0);
        lv_obj_remove_flag(h, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(h, LV_OBJ_FLAG_HIDDEN);
        s.hearts[i].img = h;
        s.hearts[i].t0 = 0;
    }
    s.flash = plain(s_box, 0xFFFFFF);
    lv_obj_set_size(s.flash, BOX_W, BOX_H);
    lv_obj_add_flag(s.flash, LV_OBJ_FLAG_HIDDEN);

    s_f = jolly_face_mood(s_mood);
    apply_mood(s_mood, true);
}

/* ---- Moods ---- */

static void targets(const jolly_face_mood_t *f, float *t)
{
    const float v[P_COUNT] = {
        [P_ESX] = f->esx, [P_ESY] = f->esy, [P_SPR] = f->spr, [P_GOX] = f->gox, [P_GOY] = f->goy,
        [P_LTL] = f->ltL, [P_LTR] = f->ltR, [P_LRL] = f->lrL, [P_LRR] = f->lrR, [P_LBL] = f->lbL,
        [P_LBR] = f->lbR, [P_LHL] = f->lhL, [P_LHR] = f->lhR, [P_CORE] = f->core, [P_BYL] = f->byL,
        [P_BYR] = f->byR, [P_BRL] = f->brL, [P_BRR] = f->brR, [P_BLUSH] = f->blush, [P_OS] = f->os,
        [P_MX] = 1, [P_MY] = 1,
    };
    memcpy(t, v, sizeof(v));
}

/* The mood's discrete parts go in at once (the mockup's CSS swaps them); its
 * numbers spring there, with a little anticipation (the mockup's set()). */
static void apply_mood(jolly_mood_t m, bool snap)
{
    jolly_mood_t prev = s_mood;
    s_mood = m;
    s_f = jolly_face_mood(m);
    targets(s_f, s_t);
    if (snap) {
        memcpy(s_x, s_t, sizeof(s_x));
        memset(s_v, 0, sizeof(s_v));
        if (m == JM_HABLANDO) {
            jolly_viseme_t v = jolly_face_viseme(.45f, false);   /* mid-word, as the mockup's sheet */
            s_x[P_MX] = s_t[P_MX] = v.mx;
            s_x[P_MY] = s_t[P_MY] = v.my;
        }
        s_g.x = s_g.y = s_g.vx = s_g.vy = s_g.tx = s_g.ty = 0;
        s_color_from = s_color_to = s_f->color;
        s_color_t0 = 0;
        s_flash_t0 = 0;
        s_blink_t0 = 0;
        s_blink_again = 0;
    } else if (m != prev) {
        bool big = m == JM_SORPRENDIDO || m == JM_ALERTA || m == JM_ASUSTADO;
        s_v[P_ESY] -= big ? .5f : .2f;
        s_v[P_ESX] += big ? .22f : .08f;
        s_v[P_BYL] += big ? 10 : 5;
        s_v[P_BYR] += big ? 10 : 5;
        s_color_from = s_color_to;
        s_color_to = s_f->color;
        s_color_t0 = s_now ? s_now : 1;
        if (m == JM_ALIVIADO || m == JM_ENOJADO) {
            s_flash_t0 = s_now ? s_now : 1;   /* a landing, a knock: the screen flashes */
        }
    }
    s_next_saccade = s_now;
    for (int i = 0; i < 2; i++) {
        uint8_t kind = i ? s_f->eye_r : s_f->eye_l;
        eye_t *e = &s.eye[i];
        show(e->body, kind == JFE_OPEN || kind == JFE_SPIRAL);
        show(e->shade, kind == JFE_OPEN || kind == JFE_SPIRAL);
        show(e->arc_up, kind == JFE_CLOSED_UP);
        show(e->arc_dn, kind == JFE_CLOSED_DOWN);
        for (int k = 0; k < 3; k++) {
            show(e->spiral[k], kind == JFE_SPIRAL);
        }
        show(e->spec, kind == JFE_OPEN);
        radius(e->body, s_f->fx & JFX_ROUND_EYES ? LV_RADIUS_CIRCLE : EYE_R);
        lv_obj_set_style_bg_grad_dir(e->body, kind == JFE_SPIRAL ? LV_GRAD_DIR_NONE : LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(e->spec, m == JM_DESCONECTADO ? 64 : 199, 0);
        lv_obj_set_style_bg_opa(e->shade, m == JM_DESCONECTADO ? 77 : 204, 0);
        e->still_since = s_now;
    }
    uint8_t mo = s_f->mouth;
    show(s.mline, mo <= JFM_WAVE);
    show(s.pill, mo == JFM_PILL);
    show(s.ring, mo == JFM_RING);
    show(s.grill, mo == JFM_GRILL);
    show(s.gtongue, s_f->fx & JFX_WAG_TONGUE);
    for (int i = 0; i < 3; i++) {
        show(s.tongue[i], s_f->fx & JFX_TONGUE);
    }
    if (mo <= JFM_WAVE) {
        int16_t xy[2 * JOLLY_MOUTH_POINTS];
        s.mn = jolly_face_mouth_line((jolly_face_mouth_t)mo, xy, JOLLY_MOUTH_POINTS);
        for (int i = 0; i < s.mn; i++) {
            s.mpts[i].x = xy[2 * i];
            s.mpts[i].y = xy[2 * i + 1];
        }
    }
    radius(s.pill, m == JM_FELIZ ? 20 : 28);
    for (int i = 0; i < 3; i++) {
        show(s.zzz[i], s_f->fx & JFX_ZZZ);
        show(s.puff[i], s_f->fx & JFX_PUFF);
    }
    show(s.drop, s_f->fx & JFX_DROP);
    for (int i = 0; i < 4; i++) {
        show(s.alarm[i], s_f->fx & JFX_ALARM);
    }
}

void jolly_eyes_set_mood(jolly_mood_t m)
{
    if (m < JM_COUNT) {
        s_pending = m;   /* jolly_eyes_tick puts it in */
    }
}

void jolly_eyes_show(jolly_mood_t m)
{
    s_forced = m < JM_COUNT ? m : JM_COUNT;
    if (s_box && m < JM_COUNT) {
        apply_mood(m, true);
    }
}

void jolly_eyes_pet(int32_t x, int32_t y)
{
    for (int i = 0; i < HEARTS; i++) {
        heart_t *h = &s.hearts[i];
        if (!h->t0) {
            h->t0 = s_now ? s_now : 1;
            h->x = x - HEART_W / 2;
            h->y = y - HEART_H / 2;
            h->drift = rnd(-26, 26);
            lv_obj_set_pos(h->img, h->x, h->y);
            obj_opa(h->img, LV_OPA_TRANSP);
            show(h->img, true);
            return;
        }
    }
}

void jolly_eyes_tap(void)
{
    jolly_eyes_pet(MOUTH_X, Y(EYE_Y));
    if (s_on_pet) {
        s_on_pet();
    }
}

/* ---- Per frame ---- */

/* Saccades, blinks and cariño's hearts, on the mockup's clocks. */
static void schedule(void)
{
    const jolly_face_mood_t *f = s_f;
    if (s_forced != JM_COUNT) {
        return;   /* held still for a picture */
    }
    if (!f->gaze_bx) {
        s_g.tx = s_g.ty = 0;
    } else if (s_now >= s_next_saccade) {
        if (frnd(0, 1) < f->gaze_big) {
            bool centre = frnd(0, 1) < .3f;
            s_g.tx = centre ? 0 : frnd(-f->gaze_bx, f->gaze_bx);
            s_g.ty = centre ? 0 : frnd(-f->gaze_by, f->gaze_by);
        } else {   /* a small drift */
            s_g.tx = fminf(fmaxf(s_g.tx + frnd(-3, 3), -f->gaze_bx), f->gaze_bx);
            s_g.ty = fminf(fmaxf(s_g.ty + frnd(-2, 2), -f->gaze_by), f->gaze_by);
        }
        s_next_saccade = s_now + rnd(f->gaze_min_ms, f->gaze_max_ms);
    }
    if (s_blink_again && s_now >= s_blink_again) {
        s_blink_t0 = s_now;   /* a double's second */
        s_blink_again = 0;
    }
    if (s_now >= s_next_blink) {
        if (f->blinks && !s_blink_t0) {
            int r = rnd(0, 99);
            s_blink_ms = r < 20 ? 170 : r < 32 ? 720 : 180;
            s_blink_t0 = s_now;
            s_blink_again = r < 20 ? s_now + 170 + 80 : 0;
        }
        s_next_blink = s_now + (s_mood == JM_PENSANDO ? rnd(3500, 6500) : rnd(1800, 5200));
    }
    if (s_mood == JM_CARINO && s_now >= s_next_heart) {
        jolly_eyes_pet(MOUTH_X + rnd(-140, 140), Y(200));
        s_next_heart = s_now + HEART_EVERY_MS;
    }
}

static float blink_now(void)
{
    if (!s_blink_t0) {
        return 0;
    }
    float p = (float)(s_now - s_blink_t0) / s_blink_ms;
    if (p >= 1) {
        s_blink_t0 = 0;
        return 0;
    }
    return jolly_face_blink(p, s_blink_ms > 400);
}

/* Talking: the pill's shape follows the reply's level; a loud syllable may
 * round into an O. Feliz laughs. Anything else rests the pill. */
static void mouth_targets(void)
{
    if (s_mood == JM_HABLANDO) {
        s_talk.lvl += (s_level - s_talk.lvl) * .35f;
        if (s_now >= s_talk.next) {
            s_talk.next = s_now + rnd(80, 170);
            s_talk.round = frnd(0, 1) < .18f;
        }
        jolly_viseme_t v = jolly_face_viseme(s_talk.lvl, s_talk.round);
        if (s_forced == JM_COUNT) {
            s_t[P_MX] = v.mx;
            s_t[P_MY] = v.my;
            s_t[P_BYL] = s_t[P_BYR] = -4 - s_talk.lvl * 6;
        }
        radius(s.pill, v.kind == JV_O ? LV_RADIUS_CIRCLE : 28);
    } else if (s_mood == JM_FELIZ) {
        s_t[P_MX] = 1;
        s_t[P_MY] = .8f + .12f * sinf((float)s_now / 75);
    } else {
        s_t[P_MX] = s_t[P_MY] = 1;
    }
}

static void springs(void)
{
    float dt = s_last ? (float)(s_now - s_last) : STEP_MS;
    s_last = s_now;
    s_step_acc += dt > 200 ? 200 : dt;
    int steps = 0;
    while (s_step_acc >= STEP_MS && steps < 12) {
        s_step_acc -= STEP_MS;
        steps++;
        bool talking = s_mood == JM_HABLANDO;
        for (int k = 0; k < P_COUNT; k++) {
            float kk = talking && (k == P_MX || k == P_MY) ? JOLLY_SPRING_K_TALK : JOLLY_SPRING_K;
            jolly_spring_step(&s_x[k], &s_v[k], s_t[k], kk, JOLLY_SPRING_DAMP);
        }
        jolly_spring_step(&s_g.x, &s_g.vx, s_g.tx, JOLLY_GAZE_K, JOLLY_GAZE_DAMP);
        jolly_spring_step(&s_g.y, &s_g.vy, s_g.ty, JOLLY_GAZE_K, JOLLY_GAZE_DAMP);
    }
    if (steps == 12) {
        s_step_acc = 0;   /* a long stall: don't run to catch up */
    }
}

static lv_color_t mood_color(void)
{
    int64_t t = s_color_t0 ? s_now - s_color_t0 : COLOR_MS;
    uint8_t mixv = t >= COLOR_MS ? 255 : (uint8_t)(t * 255 / COLOR_MS);
    return mix(s_color_to, s_color_from, mixv);
}

/* The whole face's jitter: alerta shakes, enojado shudders, asustado
 * trembles, mareado sways (without the mockup's tilt). */
static void jitter(int32_t *jx, int32_t *jy)
{
    static const int8_t SHAKE[4][2] = { { 0, 0 }, { -3, 1 }, { 3, -2 }, { -2, 2 } };
    static const int8_t RAGE[5] = { 0, -2, 2, -1, 0 };
    *jx = *jy = 0;
    if (s_forced != JM_COUNT) {
        return;
    }
    uint16_t fx = s_f->fx;
    if (fx & JFX_SHAKE) {
        int i = (int)((s_now / 70) % 4);
        *jx = SHAKE[i][0];
        *jy = SHAKE[i][1];
    } else if (fx & JFX_RAGE) {
        *jx = RAGE[(s_now / 100) % 5];
    } else if (fx & JFX_TREMBLE) {
        bool odd = (s_now / 90) % 2;
        *jx = odd ? 1 : -1;
        *jy = odd ? -1 : 1;
    } else if (fx & JFX_WOBBLE) {
        *jx = ri(-6 * cosf(2 * 3.14159f * (float)(s_now % 2200) / 2200));
    }
}

static void draw_eye(int i, float gx, float gy, float b, int32_t jx, int32_t jy, lv_color_t c)
{
    eye_t *e = &s.eye[i];
    uint8_t kind = i ? s_f->eye_r : s_f->eye_l;
    float ex = gx * .8f, ey = gy * .6f;
    float spr = i ? s_x[P_SPR] : -s_x[P_SPR];
    float cx = EYE_X[i] + ex + spr + jx, cy = Y(EYE_Y) + ey + jy;

    if (kind == JFE_CLOSED_UP || kind == JFE_CLOSED_DOWN) {
        lv_obj_t *a = kind == JFE_CLOSED_UP ? e->arc_up : e->arc_dn;
        lv_obj_set_pos(a, ri(cx) - 46, ri(cy + (kind == JFE_CLOSED_UP ? 18 : -12)) - 46);
        arc_color(a, c);
        shadow(e->body, 0, c);
        return;
    }
    float sx = s_x[P_ESX] * jolly_face_turn(gx, i) * (1 + b * .08f), sy = s_x[P_ESY] * (1 - b * .9f);
    int32_t w = ri(EYE_W * sx), h = ri(EYE_H * sy);
    w = w < 8 ? 8 : w;
    h = h < 6 ? 6 : h;
    int32_t x = ri(cx) - w / 2, y = ri(cy) - h / 2;
    bool moved = x != e->x || y != e->y || w != e->w || h != e->h;
    if (moved || (s_color_t0 && s_now - s_color_t0 < COLOR_MS)) {
        e->still_since = s_now;
    }
    e->x = x;
    e->y = y;
    e->w = w;
    e->h = h;
    place(e->body, x, y, w, h);
    place(e->shade, ri(cx - (EYE_W - 24) * sx / 2), ri(cy + 60 * sy), ri((EYE_W - 24) * sx), 16);
    bg_color(e->shade, mix(s_f->color, BG, 26));   /* on black, a faint reflection under the eye */

    /* Colour: a vertical gradient from the light top to the dark bottom (two
     * stops: LV_GRADIENT_MAX_STOPS), with a lighter rim. Mareado's eyes are
     * hollow, their spirals inside. */
    uint32_t rgb = lv_color_to_u32(c) & 0xFFFFFF;
    if (kind == JFE_SPIRAL) {
        bg_color(e->body, lv_color_hex(BG));
        border_color(e->body, c);
    } else {
        bg_color(e->body, mix(rgb, 0xFFFFFF, 160));
        bg_grad(e->body, mix(rgb, 0x000000, 200));
        border_color(e->body, mix(rgb, 0xFFFFFF, 115));
    }
    border_opa(e->body, LV_OPA_COVER);
    /* The glow only on a still eye: a moving one redraws its shadow every frame. */
    shadow(e->body, s_now - e->still_since >= GLOW_STILL_MS && s_mood != JM_DESCONECTADO ? 22 : 0, c);

    /* Inside the body: its content area starts inside the rim. */
    float kx = w / (float)EYE_W, ky = h / (float)EYE_H;
    int32_t iw = w - 2 * EYE_RIM, ih = h - 2 * EYE_RIM;
    if (kind == JFE_OPEN) {
        place(e->spec, ri((14 + gx * .3f) * kx) - EYE_RIM, ri((12 + gy * .3f) * ky) - EYE_RIM, ri(22 * kx), ri(12 * ky));
    }
    float core = s_x[P_CORE];
    show(e->core, core > .05f && kind == JFE_OPEN);
    if (core > .05f) {
        int32_t d = ri(26 * core * kx);
        place(e->core, iw / 2 - d / 2, ih / 2 - d / 2, d, d);
        radius(e->core, ri(8 * core));
    }
    if (kind == JFE_SPIRAL) {
        static const int32_t BASE[3] = { 0, 120, 240 }, SPAN[3] = { 226, 218, 216 };
        int32_t spin = (int32_t)((s_now % 1200) * 360 / 1200);
        for (int k = 0; k < 3; k++) {
            arc_angles(e->spiral[k], ((i ? -spin : spin) + BASE[k] + 720) % 360, 0, SPAN[k]);
            arc_color(e->spiral[k], c);
        }
    }

    /* Lids, in the background colour. The top one is a thick line, so it can
     * slant (enojado's V); the curved one is a big circle rising from below
     * (feliz's ^^). */
    float lt = i ? s_x[P_LTR] : s_x[P_LTL], lr = i ? s_x[P_LRR] : s_x[P_LRL];
    float lb = i ? s_x[P_LBR] : s_x[P_LBL], lh = i ? s_x[P_LHR] : s_x[P_LHL];
    bool top = lt > .02f || fabsf(lr) > 1;
    show(e->lid_t, top);
    if (top) {
        float th = (float)h, edge = lt * th - .04f * th * (1 - lt);
        float rad = lr * 3.14159f / 180, cs = cosf(rad), sn = sinf(rad);
        /* The lid is the eye's own size, rotated about its centre; its lower edge's middle: */
        float mx = w / 2.0f - sn * th / 2, my = edge - th / 2 + cs * th / 2;
        float lx = mx + sn * th / 2, ly = my - cs * th / 2;   /* the line runs th/2 above it */
        float L = (float)w;
        lv_point_precise_t p[2] = {
            { ri(lx - L * cs) - EYE_RIM, ri(ly - L * sn) - EYE_RIM },
            { ri(lx + L * cs) - EYE_RIM, ri(ly + L * sn) - EYE_RIM },
        };
        lv_obj_set_size(e->lid_t, iw, ih);
        line_width(e->lid_t, ri(th));
        line_points(e->lid_t, e->lid_pts, p, 2);
    }
    show(e->lid_b, lb > .02f);
    if (lb > .02f) {
        int32_t bh = ri(lb * h);
        place(e->lid_b, -6 - EYE_RIM, h - bh - EYE_RIM, w + 12, bh + EYE_RIM);
    }
    show(e->lid_h, lh > .02f);
    if (lh > .02f) {
        int32_t d = ri(1.48f * w);
        place(e->lid_h, (w - d) / 2 - EYE_RIM, ri(h * (1 - lh)) - EYE_RIM, d, d);
    }
}

static void draw_brow(int i, float gx, float gy, float breath, int32_t jx, int32_t jy, lv_color_t c)
{
    bar_t *b = &s.brow[i];
    float spr = i ? s_x[P_SPR] : -s_x[P_SPR];
    float cx = EYE_X[i] + gx + spr + jx;
    float cy = Y(BROW_Y) + gy * .4f + breath * 2 + (i ? s_x[P_BYR] : s_x[P_BYL]) + jy;
    float rad = (i ? s_x[P_BRR] : s_x[P_BRL]) * 3.14159f / 180;
    float dx = BROW_W / 2.0f * cosf(rad), dy = BROW_W / 2.0f * sinf(rad);
    int32_t ox = ri(cx) - 50, oy = ri(cy) - 30;
    lv_obj_set_pos(b->line, ox, oy);
    lv_point_precise_t p[2] = { { ri(50 - dx), ri(30 - dy) }, { ri(50 + dx), ri(30 + dy) } };
    line_points(b->line, b->pts, p, 2);
    line_color(b->line, c);
}

static void draw_blush(float gx, float gy, int32_t jx, int32_t jy)
{
    float a = s_x[P_BLUSH] * .85f;
    a = a < 0 ? 0 : a > 1 ? 1 : a;
    for (int i = 0; i < 6; i++) {
        bar_t *b = &s.blush[i];
        show(b->line, a > .03f);
        if (a <= .03f) {
            continue;
        }
        int side = i / 3;
        int32_t cx = ri(BLUSH_X[side] + gx * .8f * 1.05f) + jx + (i % 3 - 1) * 11;
        int32_t cy = ri(Y(BLUSH_Y) + gy * .6f) + jy;
        lv_obj_set_pos(b->line, cx - 7, cy - 12);
        line_color(b->line, lv_color_hex(s_f->blush_color));
        line_opa(b->line, (lv_opa_t)(a * 255));
    }
}

static void draw_mouth(float gx, float gy, float breath, int32_t jx, int32_t jy, lv_color_t c)
{
    float cx = MOUTH_X + gx * .5f + jx, cy = Y(MOUTH_Y) + gy * .15f + breath * 2 + jy;
    uint8_t mo = s_f->mouth;
    if (mo <= JFM_WAVE) {
        int32_t wig = 0;
        if (mo == JFM_ZIG || mo == JFM_WAVE) {   /* the nervous mouths waver */
            wig = s_forced != JM_COUNT ? 0 : ri(4 * sinf(2 * 3.14159f * (float)(s_now % 900) / 900));
        }
        lv_point_precise_t p[JOLLY_MOUTH_POINTS];
        for (int i = 0; i < s.mn; i++) {
            p[i].x = s.mpts[i].x + LINE_BOX_W / 2;
            p[i].y = s.mpts[i].y + LINE_BOX_H / 2;
        }
        lv_obj_set_pos(s.mline, ri(cx) - LINE_BOX_W / 2 + wig, ri(cy) - LINE_BOX_H / 2);
        line_points(s.mline, s.mkeep, p, s.mn);
        line_color(s.mline, c);
    } else if (mo == JFM_PILL) {
        float sx = s_x[P_MX], sy = s_x[P_MY];
        int32_t w = ri(96 * sx), h = ri(56 * sy);
        w = w < 20 ? 20 : w;
        h = h < 16 ? 16 : h;
        /* Scaled about 50 % / 30 %, as the mockup's transform-origin. */
        int32_t top = ri(cy - 24 + 16.8f * (1 - sy));
        place(s.pill, ri(cx) - w / 2, top, w, h);
        border_color(s.pill, c);
        if (s_f->fx & JFX_TONGUE) {
            int32_t iw = w - 14, ih = h - 14;
            for (int i = 0; i < 3; i++) {
                lv_obj_set_pos(s.tongue[i], iw / 2 - 15 + i * 10, ih - 3 - 10 + (i == 1 ? 0 : 5));
            }
        }
    } else if (mo == JFM_RING) {
        float os = s_x[P_OS] * (s_mood == JM_DURMIENDO ? 1 + breath * .14f : 1);
        int32_t w = ri(50 * os), h = ri(56 * os);
        w = w < 16 ? 16 : w;
        h = h < 16 ? 16 : h;
        place(s.ring, ri(cx) - w / 2, ri(cy) - h / 2, w, h);
        border_color(s.ring, c);
    } else if (mo == JFM_GRILL) {
        lv_obj_set_pos(s.grill, ri(cx) - 58, ri(cy) - 20);
        border_color(s.grill, c);
        int phase = s_forced != JM_COUNT ? 0 : (int)((s_now / 250) % 2);
        for (int i = 0; i < 6; i++) {
            bg_color(s.teeth[i], c);
            bg_opa(s.teeth[i], (i + phase) % 2 ? 115 : LV_OPA_COVER);   /* the LEDs flicker */
        }
    }
    if (s_f->fx & JFX_WAG_TONGUE) {
        int32_t wag = s_forced != JM_COUNT ? 0 : ri(3 * sinf(2 * 3.14159f * (float)(s_now % 800) / 800));
        lv_obj_set_pos(s.gtongue, ri(cx) + 4 + wag, ri(cy) + 10);
    }
}

/* The extras that run on their own clocks: zzz, the sweat drop, the sigh's
 * puffs, cariño's hearts, the alert's border, the flash. */
static void draw_extras(lv_color_t c)
{
    (void)c;
    int64_t t = s_forced != JM_COUNT ? 0 : s_now;
    if (s_f->fx & JFX_ZZZ) {
        static const int8_t DX[3] = { 0, 12, 26 }, DY[3] = { 0, -10, -22 };
        for (int i = 0; i < 3; i++) {
            float p = (float)((t + 3000 - i * 1000) % 3000) / 3000;   /* staggered by 1 s */
            if (s_forced != JM_COUNT) {
                p = .3f + i * .15f;
            }
            float a = p < .3f ? p / .3f : 1 - (p - .3f) / .7f;
            lv_obj_set_pos(s.zzz[i], 416 + DX[i] + ri(14 * p), Y(52) + DY[i] + ri(10 - 36 * p));
            obj_opa(s.zzz[i], (lv_opa_t)(a * 255));
        }
    }
    if (s_f->fx & JFX_DROP) {
        float p = s_forced != JM_COUNT ? .4f : (float)(t % 1400) / 1400;
        float a = p < .25f ? p / .25f : 1 - (p - .25f) / .75f;
        lv_obj_set_pos(s.drop, 380, Y(80) + ri(-6 + 32 * p * p));   /* ease-in */
        obj_opa(s.drop, (lv_opa_t)(a * 255));
    }
    if (s_f->fx & JFX_PUFF) {
        static const int32_t D[3] = { 10, 14, 18 };
        for (int i = 0; i < 3; i++) {
            float p = (float)((t + 2200 - i * 250) % 2200) / 2200;
            if (s_forced != JM_COUNT) {
                p = .25f + i * .2f;
            }
            float a = p < .2f ? p / .2f : 1 - (p - .2f) / .8f, k = .6f + .6f * p;
            int32_t d = ri(D[i] * k);
            place(s.puff[i], 300 + ri(46 * p) - d / 2 + D[i] / 2, Y(244) + ri(-18 * p) - d / 2 + D[i] / 2, d, d);
            obj_opa(s.puff[i], (lv_opa_t)(a * 255));
        }
    }
    for (int i = 0; i < HEARTS; i++) {
        heart_t *h = &s.hearts[i];
        if (!h->t0) {
            continue;
        }
        int64_t ht = s_now - h->t0;
        if (ht >= HEART_MS) {
            h->t0 = 0;
            show(h->img, false);
            continue;
        }
        float p = (float)ht / HEART_MS, a = p < .25f ? p / .25f : 1 - (p - .25f) / .75f;
        float rise = 1 - (1 - p) * (1 - p);   /* fast, then slowing */
        lv_obj_set_pos(h->img, h->x + ri(h->drift * p), h->y - ri(HEART_RISE * rise));
        obj_opa(h->img, (lv_opa_t)(a * 255));
    }
    if (s_f->fx & JFX_ALARM) {
        /* 1 s pulse in four steps: each step redraws only the strips. */
        static const lv_opa_t PULSE[4] = { 255, 160, 60, 160 };
        lv_opa_t a = s_forced != JM_COUNT ? 255 : PULSE[(t / 250) % 4];
        for (int i = 0; i < 4; i++) {
            bg_opa(s.alarm[i], a);
        }
    }
    bool flash = s_flash_t0 && s_now - s_flash_t0 < FLASH_MS;
    show(s.flash, flash);
    if (flash) {
        /* Stepped every 65 ms: four redraws of the face, not one per frame. */
        int step = (int)((s_now - s_flash_t0) / 65);
        obj_opa(s.flash, (lv_opa_t)(140 - step * 35));
    } else {
        s_flash_t0 = 0;
    }
}

void jolly_eyes_tick(int64_t now_ms, float level)
{
    if (!s_box) {
        return;
    }
    s_now = now_ms;
    s_level = level < 0 ? 0 : level > 1 ? 1 : level;
    if (!s_t0) {
        s_t0 = now_ms;
        s_next_blink = now_ms + rnd(1500, 4000);
    }
    jolly_mood_t want = s_forced != JM_COUNT ? s_forced : s_pending;
    if (want != s_mood) {
        apply_mood(want, false);
    }
    schedule();
    mouth_targets();
    if (s_forced == JM_COUNT) {
        springs();
    } else {
        s_last = s_now;
    }
    float b = s_forced != JM_COUNT ? 0 : blink_now();
    float gx = s_g.x + s_x[P_GOX], gy = s_g.y + s_x[P_GOY];
    float breath = s_forced != JM_COUNT ? 0 : sinf(2 * 3.14159f * (float)((now_ms - s_t0) % BREATH_MS) / BREATH_MS);
    int32_t jx, jy;
    jitter(&jx, &jy);
    lv_color_t c = mood_color();
    if (s_color_t0 && now_ms - s_color_t0 >= COLOR_MS) {
        s_color_t0 = 0;
    }
    draw_blush(gx, gy, jx, jy);
    for (int i = 0; i < 2; i++) {
        draw_eye(i, gx, gy, b, jx, jy, c);
        draw_brow(i, gx, gy, breath, jx, jy, c);
    }
    draw_mouth(gx, gy, breath, jx, jy, c);
    draw_extras(c);
}
