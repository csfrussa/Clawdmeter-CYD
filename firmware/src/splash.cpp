#include "splash.h"
#include "splash_animations.h"
#include "theme.h"
#include "usage_rate.h"
#include <Arduino.h>
#include <string.h>

// 20×20 pixel-art grid scaled 10× → 200×200 canvas, centred in 320×240 display.
// Buffer: 200×200×2 = 80 KB allocated from internal heap (no PSRAM needed).
#define GRID         20
#define CELL         10
#define CANVAS_W     (GRID * CELL)   // 200
#define CANVAS_H     (GRID * CELL)   // 200

#define COL_EMPTY    0x0000   // true black — matches THEME_BG

LV_FONT_DECLARE(font_styrene_14);

static lv_obj_t* splash_container = NULL;
static lv_obj_t* canvas           = NULL;
static lv_obj_t* label_status     = NULL;
static uint16_t* canvas_buf       = NULL;  // 200×200 RGB565, internal heap

static uint16_t cur_anim       = 0;
static uint16_t cur_frame      = 0;
static uint32_t frame_started_ms = 0;
static uint32_t last_pick_ms   = 0;
static bool     active         = false;

#define SPLASH_ROTATE_INTERVAL_MS 20000

#define GROUP_COUNT 4
#define GROUP_MAX   4
static int8_t  group_lists[GROUP_COUNT][GROUP_MAX];
static uint8_t group_size[GROUP_COUNT]     = {0};
static uint8_t group_rotation[GROUP_COUNT] = {0};

static const char* GROUP_NAMES[GROUP_COUNT][GROUP_MAX] = {
    { "expression sleep", "idle breathe", "idle blink", "expression wink" },
    { "idle look around", "work think",   "work coding", NULL },
    { "dance sway",       "expression surprise", "dance bounce", NULL },
    { "dance bounce dj",  "dance sway dj", "dance djmix", NULL },
};

static void resolve_group_lists(void) {
    for (int g = 0; g < GROUP_COUNT; g++) {
        group_size[g] = 0;
        for (int s = 0; s < GROUP_MAX; s++) {
            group_lists[g][s] = -1;
            const char* want = GROUP_NAMES[g][s];
            if (!want) continue;
            for (int i = 0; i < SPLASH_ANIM_COUNT; i++) {
                if (strcmp(splash_anims[i].name, want) == 0) {
                    group_lists[g][group_size[g]++] = (int8_t)i;
                    break;
                }
            }
        }
    }
}

static void render_frame(const uint8_t* cells, const uint16_t* palette) {
    for (int gy = 0; gy < GRID; gy++) {
        uint16_t row[CANVAS_W];
        for (int gx = 0; gx < GRID; gx++) {
            uint8_t code  = cells[gy * GRID + gx];
            uint16_t color = (palette && code < SPLASH_PALETTE_SIZE)
                             ? palette[code] : COL_EMPTY;
            uint16_t* p = &row[gx * CELL];
            for (int i = 0; i < CELL; i++) p[i] = color;
        }
        for (int dy = 0; dy < CELL; dy++) {
            memcpy(&canvas_buf[(gy * CELL + dy) * CANVAS_W], row, CANVAS_W * 2);
        }
    }
    if (canvas) lv_obj_invalidate(canvas);
}

static void show_placeholder(void) {
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) canvas_buf[i] = COL_EMPTY;
    if (canvas)       lv_obj_invalidate(canvas);
    if (label_status) lv_obj_clear_flag(label_status, LV_OBJ_FLAG_HIDDEN);
}

void splash_init(lv_obj_t* parent) {
    canvas_buf = (uint16_t*)malloc(CANVAS_W * CANVAS_H * 2);
    if (!canvas_buf) {
        Serial.println("splash: failed to alloc canvas buffer");
        return;
    }

    // Full-screen container (same bg as theme)
    splash_container = lv_obj_create(parent);
    lv_obj_set_size(splash_container, 320, 240);
    lv_obj_set_pos(splash_container, 0, 0);
    lv_obj_set_style_bg_color(splash_container, THEME_BG, 0);
    lv_obj_set_style_bg_opa(splash_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(splash_container, 0, 0);
    lv_obj_set_style_pad_all(splash_container, 0, 0);
    lv_obj_clear_flag(splash_container, LV_OBJ_FLAG_SCROLLABLE);

    // Canvas centred in 320×240 — 200×200 leaves 60 px sides, 20 px top/bottom
    canvas = lv_canvas_create(splash_container);
    lv_canvas_set_buffer(canvas, canvas_buf, CANVAS_W, CANVAS_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_center(canvas);

    label_status = lv_label_create(splash_container);
    lv_label_set_text(label_status,
        "no animations loaded\n\n"
        "run tools/scrape_claudepix.js\n"
        "then tools/convert_to_c.js");
    lv_obj_set_style_text_font(label_status, &font_styrene_14, 0);
    lv_obj_set_style_text_color(label_status, lv_color_hex(0xb0aea5), 0);
    lv_obj_set_style_text_align(label_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label_status);

    resolve_group_lists();

    if (SPLASH_ANIM_COUNT == 0) {
        show_placeholder();
    } else {
        lv_obj_add_flag(label_status, LV_OBJ_FLAG_HIDDEN);
        const splash_anim_def_t* a = &splash_anims[0];
        render_frame(a->frames[0], a->palette);
        frame_started_ms = millis();
    }

    lv_obj_add_flag(splash_container, LV_OBJ_FLAG_HIDDEN);
}

void splash_tick(void) {
    if (!active || SPLASH_ANIM_COUNT == 0) return;

    if (millis() - last_pick_ms >= SPLASH_ROTATE_INTERVAL_MS) {
        splash_pick_for_current_rate();
    }

    const splash_anim_def_t* a = &splash_anims[cur_anim];
    if (a->frame_count == 0) return;

    uint16_t hold = a->holds[cur_frame];
    if (millis() - frame_started_ms >= hold) {
        cur_frame = (cur_frame + 1) % a->frame_count;
        frame_started_ms = millis();
        render_frame(a->frames[cur_frame], a->palette);
    }
}

void splash_next(void) {
    if (SPLASH_ANIM_COUNT == 0) return;
    cur_anim = (cur_anim + 1) % SPLASH_ANIM_COUNT;
    cur_frame = 0;
    frame_started_ms = millis();
    last_pick_ms     = frame_started_ms;
    const splash_anim_def_t* a = &splash_anims[cur_anim];
    render_frame(a->frames[0], a->palette);
    Serial.printf("splash: -> %s\n", a->name);
}

void splash_pick_for_current_rate(void) {
    if (SPLASH_ANIM_COUNT == 0) return;
    int g = usage_rate_group();
    if (g < 0 || g >= GROUP_COUNT) g = 0;
    if (group_size[g] == 0) return;

    uint8_t slot = group_rotation[g] % group_size[g];
    group_rotation[g]++;
    int8_t idx = group_lists[g][slot];
    if (idx < 0) return;

    cur_anim         = (uint16_t)idx;
    cur_frame        = 0;
    frame_started_ms = millis();
    last_pick_ms     = frame_started_ms;
    const splash_anim_def_t* a = &splash_anims[cur_anim];
    render_frame(a->frames[0], a->palette);
}

bool splash_is_active(void) { return active; }

void splash_show(void) {
    splash_pick_for_current_rate();
    if (splash_container) lv_obj_clear_flag(splash_container, LV_OBJ_FLAG_HIDDEN);
    active = true;
}

void splash_hide(void) {
    if (splash_container) lv_obj_add_flag(splash_container, LV_OBJ_FLAG_HIDDEN);
    active = false;
}

lv_obj_t* splash_get_root(void) { return splash_container; }
