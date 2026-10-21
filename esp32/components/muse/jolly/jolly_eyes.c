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
#include "jolly/jolly_gesture.h"
#include "jolly/jolly_mouth.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

/* At the mockup's scale (480x320). */
#define EYE_W 96
#define EYE_H 120
#define EYE_GAP 60                 /* between the eyes */
#define BOX_W 360                  /* room for the gaze and the hearts */
#define BOX_H 264                  /* ... and the mouth */
#define EYES_DY (-26)              /* the eyes' centre, above the box's: room for the mouth */
#define MOUTH_DY 80                /* the mouth's centre, below it */
#define HEARTS 6
#define HEART_W 20
#define HEART_H 18
#define HEART_MS 1200              /* rising 60 px and fading */
#define HEART_RISE 60
#define HEART_EVERY_MS 650         /* in cariño */
#define SACCADE_MS 120
#define COLOR_MS 180
#define LID_EASE 0.35f             /* per frame, towards the mood's lids */

typedef struct {
    uint32_t color;
    float w, h;                    /* eye size, times EYE_W and EYE_H */
    float top, bottom;             /* lids, as a fraction of the eye's height */
    float smile;                   /* the curved lower lid of feliz and cariño, 0..1 */
    int16_t bx, by;                /* gaze range, px */
    float big;                     /* chance a saccade uses the full range */
    int16_t min_ms, max_ms;        /* between saccades; 0 = still */
    bool blinks;
} mood_t;

/* Colours and gaze ranges from the guardian plan (monitor-jolly mockup). */
static const mood_t MOODS[JM_COUNT] = {
    [JM_TRANQUILO]    = { 0x8BEBFF, 1.00f, 1.00f, 0.00f, 0.00f, 0.0f, 26, 14, .55f, 650, 2600, true },
    [JM_ESCUCHANDO]   = { 0x5CE1FF, 1.06f, 1.06f, 0.00f, 0.00f, 0.0f, 5, 4, .25f, 900, 2200, true },
    [JM_PENSANDO]     = { 0x9FDBFF, 1.00f, 1.00f, 0.22f, 0.00f, 0.0f, 12, 6, .35f, 1000, 2600, true },
    [JM_HABLANDO]     = { 0x6FE6FF, 1.00f, 1.00f, 0.00f, 0.00f, 0.0f, 6, 4, .30f, 700, 1800, false },
    [JM_FELIZ]        = { 0x8CF5E0, 1.04f, 1.00f, 0.00f, 0.00f, 1.0f, 10, 6, .40f, 800, 2000, true },
    [JM_CARINO]       = { 0xFF9ECF, 1.04f, 1.00f, 0.00f, 0.00f, 1.0f, 6, 4, .30f, 900, 2000, true },
    [JM_DURMIENDO]    = { 0x6F86C9, 1.00f, 1.00f, 0.82f, 0.00f, 0.0f, 0, 0, 0.0f, 0, 0, false },
    [JM_ESPIA]        = { 0xFFC145, 1.06f, 1.00f, 0.34f, 0.30f, 0.0f, 10, 4, .40f, 900, 2000, true },
    [JM_ALERTA]       = { 0xFF5C6C, 1.00f, 1.12f, 0.00f, 0.00f, 0.0f, 20, 10, .85f, 220, 600, true },
    [JM_DESCONECTADO] = { 0x5D6380, 1.00f, 0.12f, 0.00f, 0.00f, 0.0f, 0, 0, 0.0f, 0, 0, false },
    /* From the IMU; first cuts, until the face's redesign. */
    [JM_MAREADO]      = { 0xC59BFF, 1.00f, 0.96f, 0.10f, 0.10f, 0.0f, 24, 16, 1.0f, 140, 320, false },
    [JM_ASUSTADO]     = { 0xE8F4FF, 1.06f, 1.18f, 0.00f, 0.00f, 0.0f, 3, 3, 0.2f, 120, 300, false },
    [JM_ALIVIADO]     = { 0x8CF5E0, 1.02f, 1.00f, 0.00f, 0.00f, 0.6f, 8, 5, .30f, 900, 2200, true },
    [JM_ENOJADO]      = { 0xFF8A4C, 1.04f, 0.92f, 0.36f, 0.00f, 0.0f, 6, 3, .30f, 400, 1200, true },
};

typedef struct {
    lv_obj_t *o, *top, *bottom, *smile;
    int32_t w, h, x, y, top_h, bottom_h, smile_y;   /* last set: only changes redraw */
    uint32_t color;
} eye_t;

typedef struct {
    lv_obj_t *img;
    int64_t t0;                    /* 0: free */
    int32_t x, y;
} heart_t;

static lv_obj_t *s_box;
static eye_t s_eye[2];
static heart_t s_hearts[HEARTS];
static lv_obj_t *s_zzz;
static lv_obj_t *s_arc;            /* the smile */
static lv_obj_t *s_mouth;          /* line, ring or talking mouth */
static lv_obj_t *s_bubble;         /* durmiendo's snore bubble */
static struct {
    int kind, w, h, x, y, thick, bubble;   /* last set: only changes redraw */
    uint32_t color;
} s_m = { .kind = -1 };
static float s_level;              /* the reply's level, 0..1, for the talking mouth */
static void (*s_on_pet)(void);
static void (*s_on_hold)(bool down);
static jolly_gesture_t s_gesture;

static jolly_mood_t s_mood = JM_TRANQUILO, s_pending = JM_TRANQUILO;
static uint32_t s_color_from, s_color_to;
static int64_t s_color_t0 = -1, s_now;
static float s_top, s_bottom, s_smile;   /* eased lids */

/* Gaze: from -> to over SACCADE_MS. */
static float s_gx0, s_gy0, s_gx1, s_gy1;
static int64_t s_gaze_t0, s_next_saccade;

/* Blink: kind 0 none, 1 single, 2 double, 3 slow. */
static int s_blink;
static int64_t s_blink_t0, s_next_blink, s_next_heart;
static bool s_shut;                /* this blink reached its closed point */

static uint8_t s_heart_a8[HEART_W * HEART_H];
static lv_image_dsc_t s_heart_dsc;

uint32_t jolly_mood_color(jolly_mood_t m)
{
    return m < JM_COUNT ? MOODS[m].color : MOODS[JM_TRANQUILO].color;
}

static int32_t rnd(int32_t lo, int32_t hi)
{
    return hi > lo ? (int32_t)lv_rand(lo, hi) : lo;
}

/* A heart's alpha mask: the classic implicit curve, (x^2 + y^2 - 1)^3 <= x^2 y^3. */
static void make_heart(void)
{
    for (int py = 0; py < HEART_H; py++) {
        for (int px = 0; px < HEART_W; px++) {
            float x = (px + 0.5f - HEART_W / 2.0f) / (HEART_W / 2.2f);
            float y = (HEART_H / 2.0f - (py + 0.5f)) / (HEART_H / 2.4f) + 0.15f;
            float a = x * x + y * y - 1.0f;
            s_heart_a8[py * HEART_W + px] = a * a * a - x * x * y * y * y <= 0.0f ? 0xFF : 0x00;
        }
    }
    memset(&s_heart_dsc, 0, sizeof(s_heart_dsc));
    s_heart_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_heart_dsc.header.cf = LV_COLOR_FORMAT_A8;
    s_heart_dsc.header.w = HEART_W;
    s_heart_dsc.header.h = HEART_H;
    s_heart_dsc.header.stride = HEART_W;
    s_heart_dsc.data = s_heart_a8;
    s_heart_dsc.data_size = sizeof(s_heart_a8);
}

static lv_obj_t *plain(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

/* Every touch on the face goes through the gesture classifier: a tap pets,
 * a hold talks, a swipe is the tileview's. */
static void on_box_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    jolly_gesture_sample_t s = code == LV_EVENT_RELEASED ? JGS_RELEASED
                             : code == LV_EVENT_PRESS_LOST ? JGS_LOST
                                                           : JGS_PRESSED;
    lv_point_t p = { 0, 0 };
    lv_area_t a;
    lv_indev_t *indev = lv_indev_active();
    if (indev) {
        lv_indev_get_point(indev, &p);
    }
    lv_obj_get_coords(s_box, &a);
    switch (jolly_gesture_feed(&s_gesture, s, p.x - a.x1, p.y - a.y1, (int64_t)lv_tick_get())) {
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

void jolly_eyes_create(lv_obj_t *parent, int32_t y, void (*on_pet)(void), void (*on_hold)(bool down))
{
    s_on_pet = on_pet;
    s_on_hold = on_hold;
    jolly_gesture_reset(&s_gesture);
    make_heart();
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
    lv_obj_align(s_box, LV_ALIGN_CENTER, 0, y);
    for (int i = 0; i < 2; i++) {
        eye_t *e = &s_eye[i];
        e->o = plain(s_box);
        lv_obj_set_style_clip_corner(e->o, true, 0);   /* the lids follow the rounded corners */
        e->top = plain(e->o);
        e->bottom = plain(e->o);
        e->smile = plain(e->o);
        lv_obj_set_style_radius(e->smile, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(e->top, lv_color_black(), 0);
        lv_obj_set_style_bg_color(e->bottom, lv_color_black(), 0);
        lv_obj_set_style_bg_color(e->smile, lv_color_black(), 0);
    }
    s_arc = lv_arc_create(s_box);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_opa(s_arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(s_arc, 0, 0);
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_arc_set_angles(s_arc, 35, 145);   /* the bottom of the circle: a smile */
    s_mouth = plain(s_box);
    s_bubble = plain(s_box);
    lv_obj_set_style_bg_opa(s_bubble, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_bubble, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_bubble, 2, 0);
    lv_obj_add_flag(s_bubble, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < HEARTS; i++) {
        lv_obj_t *h = lv_image_create(s_box);
        lv_image_set_src(h, &s_heart_dsc);
        lv_obj_set_style_image_recolor(h, lv_color_hex(MOODS[JM_CARINO].color), 0);
        lv_obj_set_style_image_recolor_opa(h, LV_OPA_COVER, 0);
        lv_obj_remove_flag(h, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(h, LV_OBJ_FLAG_HIDDEN);
        s_hearts[i].img = h;
    }
    s_zzz = lv_label_create(s_box);
    lv_label_set_text(s_zzz, "z z z");
    lv_obj_set_style_text_color(s_zzz, lv_color_hex(MOODS[JM_DURMIENDO].color), 0);
    lv_obj_add_flag(s_zzz, LV_OBJ_FLAG_HIDDEN);
    s_color_from = s_color_to = MOODS[s_mood].color;
}

/* The mood takes over: its colour fades in, its lids ease in. */
static void switch_mood(void)
{
    if (s_pending == s_mood) {
        return;
    }
    s_color_from = s_color_to;
    s_color_to = MOODS[s_pending].color;
    s_color_t0 = s_now;
    s_mood = s_pending;
    s_next_saccade = s_now;            /* look somewhere new */
    lv_obj_set_flag(s_zzz, LV_OBJ_FLAG_HIDDEN, s_mood != JM_DURMIENDO);
}

void jolly_eyes_set_mood(jolly_mood_t m)
{
    if (m < JM_COUNT) {
        s_pending = m;   /* jolly_eyes_tick puts it in behind a blink */
    }
}

static void start_blink(int kind)
{
    s_blink = kind;
    s_blink_t0 = s_now;
    s_shut = false;
}

void jolly_eyes_pet(int32_t x, int32_t y)
{
    for (int i = 0; i < HEARTS; i++) {
        if (!s_hearts[i].t0) {
            s_hearts[i].t0 = s_now ? s_now : 1;
            s_hearts[i].x = x - HEART_W / 2;
            s_hearts[i].y = y - HEART_H / 2;
            lv_obj_remove_flag(s_hearts[i].img, LV_OBJ_FLAG_HIDDEN);
            return;
        }
    }
}

void jolly_eyes_tap(void)
{
    jolly_eyes_pet(BOX_W / 2, BOX_H / 2 + EYES_DY);
    if (s_on_pet) {
        s_on_pet();
    }
}

/* Closure 0 (open) .. 1 (shut); a little below 0 on reopening is the stretch. */
static float blink_closure(void)
{
    static const int CLOSE[] = { 0, 70, 70, 260 }, OPEN[] = { 0, 110, 110, 320 };
    if (!s_blink) {
        return 0.0f;
    }
    int64_t t = s_now - s_blink_t0;
    int close = CLOSE[s_blink], open = OPEN[s_blink];
    if (t < close) {
        return (float)t / close;
    }
    if (!s_shut) {
        s_shut = true;
        switch_mood();                 /* shut: nobody sees a pending mood go in */
    }
    t -= close;
    if (t < open) {
        float u = (float)t / open;
        return (1.0f - u) - 0.06f * sinf(u * 3.14159f);   /* back, 6 % past open */
    }
    if (s_blink == 2) {                /* a double: once more */
        start_blink(1);
        return 0.0f;
    }
    s_blink = 0;
    return 0.0f;
}

static void schedule(void)
{
    const mood_t *m = &MOODS[s_mood];
    if (m->max_ms && s_now >= s_next_saccade) {
        float range = (int32_t)lv_rand(0, 99) < (int32_t)(m->big * 100) ? 1.0f : 0.35f;
        s_gx0 = s_gx1;
        s_gy0 = s_gy1;
        s_gx1 = rnd(-m->bx, m->bx) * range;
        s_gy1 = rnd(-m->by, m->by) * range;
        s_gaze_t0 = s_now;
        s_next_saccade = s_now + rnd(m->min_ms, m->max_ms);
    } else if (!m->max_ms) {
        s_gx0 = s_gx1 = 0;
        s_gy0 = s_gy1 = 0;
    }
    if (s_pending != s_mood && !s_blink) {
        if (m->blinks) {
            start_blink(1);            /* the new mood goes in behind it */
        } else {
            switch_mood();             /* eyes that don't blink change in the open */
        }
    } else if (m->blinks && !s_blink && s_now >= s_next_blink) {
        int r = rnd(0, 99);
        start_blink(r < 70 ? 1 : r < 90 ? 2 : 3);
        s_next_blink = s_now + rnd(2200, 5600);
    }
    if (s_mood == JM_CARINO && s_now >= s_next_heart) {
        jolly_eyes_pet(rnd(BOX_W / 2 - 110, BOX_W / 2 + 110), BOX_H / 2 + EYES_DY - EYE_H / 2);
        s_next_heart = s_now + HEART_EVERY_MS;
    }
}

static lv_color_t mood_color(void)
{
    int64_t t = s_color_t0 < 0 ? COLOR_MS : s_now - s_color_t0;
    uint8_t mix = t >= COLOR_MS ? 255 : (uint8_t)(t * 255 / COLOR_MS);
    return lv_color_mix(lv_color_hex(s_color_to), lv_color_hex(s_color_from), mix);
}

static void place_eye(eye_t *e, int side, float closure, float gx, float gy, lv_color_t color)
{
    const mood_t *m = &MOODS[s_mood];
    float pulse = 1.0f;
    if (s_mood == JM_HABLANDO) {
        pulse = 1.0f + 0.08f * sinf((float)s_now * 0.0105f);   /* about 1.7 Hz, timed */
    } else if (s_mood == JM_DURMIENDO) {
        pulse = 1.0f + 0.05f * sinf((float)s_now * 0.0021f);   /* slow breathing */
    }
    int32_t w = (int32_t)(EYE_W * m->w * (1.0f + 0.12f * closure));
    int32_t h = (int32_t)(EYE_H * m->h * pulse * (1.0f - 0.9f * closure));
    h = h < 4 ? 4 : h;
    int32_t x = (side ? 1 : -1) * (EYE_GAP / 2 + EYE_W / 2) + (int32_t)gx, y = EYES_DY + (int32_t)gy;
    int32_t top = (int32_t)(s_top * h), bottom = (int32_t)(s_bottom * h);
    /* Smile: a big circle rising into the eye from below, leaving an arch. */
    int32_t smile_y = s_smile < 0.02f ? -1 : h - (int32_t)(s_smile * h * 0.55f);
    uint32_t rgb = lv_color_to_u32(color);
    if (w == e->w && h == e->h && x == e->x && y == e->y && top == e->top_h && bottom == e->bottom_h &&
        smile_y == e->smile_y && rgb == e->color) {
        return;   /* still: nothing to redraw */
    }
    if (w != e->w || h != e->h) {
        lv_obj_set_size(e->o, w, h);
        lv_obj_set_style_radius(e->o, h * 28 / 100, 0);
    }
    if (rgb != e->color) {
        lv_obj_set_style_bg_color(e->o, color, 0);
    }
    lv_obj_align(e->o, LV_ALIGN_CENTER, x, y);
    lv_obj_set_size(e->top, w, top);
    lv_obj_align(e->top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flag(e->top, LV_OBJ_FLAG_HIDDEN, top <= 0);
    lv_obj_set_size(e->bottom, w, bottom);
    lv_obj_align(e->bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flag(e->bottom, LV_OBJ_FLAG_HIDDEN, bottom <= 0);
    int32_t d = w * 17 / 10;
    lv_obj_set_size(e->smile, d, d);
    lv_obj_align(e->smile, LV_ALIGN_TOP_MID, 0, smile_y < 0 ? h : smile_y);
    lv_obj_set_flag(e->smile, LV_OBJ_FLAG_HIDDEN, smile_y < 0);
    e->w = w;
    e->h = h;
    e->x = x;
    e->y = y;
    e->top_h = top;
    e->bottom_h = bottom;
    e->smile_y = smile_y;
    e->color = rgb;
}

/* The mouth for the mood showing, in the eyes' colour, following their gaze a little. */
static void place_mouth(lv_color_t color, float gx, float gy)
{
    jolly_mouth_t m = jolly_mouth_for(s_mood);
    int32_t x = m.dx + (int32_t)(gx * 0.4f), y = MOUTH_DY + (int32_t)(gy * 0.4f);
    if (m.jitter) {
        x += rnd(-2, 2);
        y += rnd(-2, 2);
    }
    int32_t h = m.kind == JMOUTH_TALK ? jolly_mouth_talk_h(s_level, s_now)
              : m.kind == JMOUTH_O    ? m.w
                                      : m.thick;
    int bubble = m.bubble ? 6 + (int)((s_now / 120) % 10) : 0;   /* a snore bubble swelling */
    uint32_t rgb = lv_color_to_u32(color);
    if ((int)m.kind == s_m.kind && m.w == s_m.w && h == s_m.h && x == s_m.x && y == s_m.y && m.thick == s_m.thick &&
        bubble == s_m.bubble && rgb == s_m.color) {
        return;   /* still: nothing to redraw */
    }
    bool smile = m.kind == JMOUTH_SMILE || m.kind == JMOUTH_FROWN;
    lv_obj_set_flag(s_arc, LV_OBJ_FLAG_HIDDEN, !smile);
    lv_obj_set_flag(s_mouth, LV_OBJ_FLAG_HIDDEN, smile);
    if (smile) {
        /* A smile is the bottom of a circle sitting above the mouth line; a frown,
         * the top of one sitting below it. */
        bool frown = m.kind == JMOUTH_FROWN;
        lv_arc_set_angles(s_arc, frown ? 215 : 35, frown ? 325 : 145);
        lv_obj_set_size(s_arc, m.w, m.w);
        lv_obj_set_style_arc_width(s_arc, m.thick, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(s_arc, color, LV_PART_INDICATOR);
        lv_obj_align(s_arc, LV_ALIGN_CENTER, x, frown ? y + m.w / 2 - m.thick : y - m.w / 2 + m.thick);
    } else {
        lv_obj_set_size(s_mouth, m.w, h);
        lv_obj_set_style_radius(s_mouth, m.kind == JMOUTH_O ? LV_RADIUS_CIRCLE : (h < m.w ? h / 2 : m.w / 2), 0);
        bool ring = m.kind == JMOUTH_O;
        lv_obj_set_style_bg_opa(s_mouth, ring ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_mouth, color, 0);
        lv_obj_set_style_border_width(s_mouth, ring ? m.thick : 0, 0);
        lv_obj_set_style_border_color(s_mouth, color, 0);
        lv_obj_align(s_mouth, LV_ALIGN_CENTER, x, y);
    }
    lv_obj_set_flag(s_bubble, LV_OBJ_FLAG_HIDDEN, !bubble);
    if (bubble) {
        lv_obj_set_size(s_bubble, bubble, bubble);
        lv_obj_set_style_border_color(s_bubble, color, 0);
        lv_obj_align(s_bubble, LV_ALIGN_CENTER, x + m.w / 2 + 6 + bubble / 2, y - 4 - bubble / 2);
    }
    s_m.kind = m.kind;
    s_m.w = m.w;
    s_m.h = h;
    s_m.x = x;
    s_m.y = y;
    s_m.thick = m.thick;
    s_m.bubble = bubble;
    s_m.color = rgb;
}

static void place_hearts(void)
{
    for (int i = 0; i < HEARTS; i++) {
        heart_t *h = &s_hearts[i];
        if (!h->t0) {
            continue;
        }
        int64_t t = s_now - h->t0;
        if (t >= HEART_MS) {
            h->t0 = 0;
            lv_obj_add_flag(h->img, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_align(h->img, LV_ALIGN_TOP_LEFT, h->x, h->y - (int32_t)(t * HEART_RISE / HEART_MS));
        lv_obj_set_style_opa(h->img, (lv_opa_t)(255 - t * 255 / HEART_MS), 0);
    }
}

void jolly_eyes_tick(int64_t now_ms, float level)
{
    if (!s_box) {
        return;
    }
    s_now = now_ms;
    s_level = level;
    if (!s_next_blink) {
        s_next_blink = now_ms + rnd(1500, 4000);
    }
    schedule();
    float closure = blink_closure();
    const mood_t *m = &MOODS[s_mood];
    s_top += (m->top - s_top) * LID_EASE;
    s_bottom += (m->bottom - s_bottom) * LID_EASE;
    s_smile += (m->smile - s_smile) * LID_EASE;

    float u = (float)(now_ms - s_gaze_t0) / SACCADE_MS;
    u = u > 1.0f ? 1.0f : u;
    u = u * u * (3.0f - 2.0f * u);     /* ease in and out */
    float gx = s_gx0 + (s_gx1 - s_gx0) * u, gy = s_gy0 + (s_gy1 - s_gy0) * u;
    if (s_mood == JM_CARINO) {
        gx += 8.0f * sinf((float)now_ms * 0.005f);   /* sway */
    } else if (s_mood == JM_ALERTA) {
        gx += rnd(-2, 2);              /* jitter */
        gy += rnd(-2, 2);
    }
    lv_color_t color = mood_color();
    for (int i = 0; i < 2; i++) {
        place_eye(&s_eye[i], i, closure, gx, gy, color);
    }
    place_mouth(color, gx, gy);
    if (s_mood == JM_DURMIENDO) {
        int32_t rise = (int32_t)((now_ms / 40) % 40);
        lv_obj_align(s_zzz, LV_ALIGN_TOP_RIGHT, -20, 30 - rise);
    }
    place_hearts();
}
