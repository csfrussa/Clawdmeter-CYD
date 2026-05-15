#include "ui.h"
#include "splash.h"
#include <lvgl.h>
#include "display_cfg.h"
#include "theme.h"

// Fonts compiled for CYD's 320×240 ILI9341 (~160 PPI)
LV_FONT_DECLARE(font_tiempos_34);
LV_FONT_DECLARE(font_styrene_28);
LV_FONT_DECLARE(font_styrene_24);
LV_FONT_DECLARE(font_styrene_20);
LV_FONT_DECLARE(font_styrene_16);
LV_FONT_DECLARE(font_styrene_14);
LV_FONT_DECLARE(font_styrene_12);
LV_FONT_DECLARE(font_mono_18);

#include "theme.h"
#define COL_BG      THEME_BG
#define COL_PANEL   THEME_PANEL
#define COL_TEXT    THEME_TEXT
#define COL_DIM     THEME_DIM
#define COL_ACCENT  THEME_ACCENT
#define COL_GREEN   THEME_GREEN
#define COL_AMBER   THEME_AMBER
#define COL_RED     THEME_RED
#define COL_BAR_BG  THEME_BAR_BG

// ---- Layout constants for 320×240 (ILI9341 landscape) ----
#define SCR_W       320
#define SCR_H       240
#define MARGIN       10
#define TITLE_Y       6
#define CONTENT_Y    48
#define CONTENT_W   (SCR_W - 2 * MARGIN)  // 300
#define PANEL_H      80
#define PANEL_GAP     8

// ---- Usage screen widgets ----
static lv_obj_t* usage_container;
static lv_obj_t* lbl_title;
static lv_obj_t* bar_session;
static lv_obj_t* lbl_session_pct;
static lv_obj_t* lbl_session_reset;
static lv_obj_t* bar_weekly;
static lv_obj_t* lbl_weekly_pct;
static lv_obj_t* lbl_weekly_reset;
static lv_obj_t* lbl_anim;

// ---- WiFi screen widgets ----
static lv_obj_t* wifi_container;
static lv_obj_t* lbl_wifi_status;
static lv_obj_t* lbl_wifi_ssid;
static lv_obj_t* lbl_wifi_ip;
static lv_obj_t* lbl_wifi_broker;

// ---- Shared ----
static screen_t current_screen = SCREEN_USAGE;

// Animation state
static uint32_t anim_last_ms    = 0;
static uint8_t  anim_spinner_idx = 0;
static uint8_t  anim_phase      = 0;
static uint8_t  anim_msg_idx    = 0;
static uint32_t anim_msg_start  = 0;
#define ANIM_MSG_MS  4000

static const char* const spinner_frames[] = {
    "\xC2\xB7", "\xE2\x9C\xBB", "\xE2\x9C\xBD",
    "\xE2\x9C\xB6", "\xE2\x9C\xB3", "\xE2\x9C\xA2",
};
#define SPINNER_COUNT  6
#define SPINNER_PHASES (2 * (SPINNER_COUNT - 1))

static const uint16_t spinner_ms[SPINNER_COUNT] = {
    260, 130, 130, 130, 130, 260,
};

static const char* const anim_messages[] = {
    "Accomplishing", "Elucidating", "Perusing",
    "Actioning", "Enchanting", "Philosophising",
    "Actualizing", "Envisioning", "Pondering",
    "Baking", "Finagling", "Pontificating",
    "Booping", "Flibbertigibbeting", "Processing",
    "Brewing", "Forging", "Puttering",
    "Calculating", "Forming", "Puzzling",
    "Cerebrating", "Frolicking", "Reticulating",
    "Channelling", "Generating", "Ruminating",
    "Churning", "Germinating", "Scheming",
    "Clauding", "Hatching", "Schlepping",
    "Coalescing", "Herding", "Shimmying",
    "Cogitating", "Honking", "Shucking",
    "Combobulating", "Hustling", "Simmering",
    "Computing", "Ideating", "Smooshing",
    "Concocting", "Imagining", "Spelunking",
    "Conjuring", "Incubating", "Spinning",
    "Considering", "Inferring", "Stewing",
    "Contemplating", "Jiving", "Sussing",
    "Cooking", "Manifesting", "Synthesizing",
    "Crafting", "Marinating", "Thinking",
    "Creating", "Meandering", "Tinkering",
    "Crunching", "Moseying", "Transmuting",
    "Deciphering", "Mulling", "Unfurling",
    "Deliberating", "Mustering", "Unravelling",
    "Determining", "Musing", "Vibing",
    "Discombobulating", "Noodling", "Wandering",
    "Divining", "Percolating", "Whirring",
    "Doing", "Wibbling",
    "Effecting", "Wizarding",
    "Working", "Wrangling",
};
#define ANIM_MSG_COUNT (sizeof(anim_messages) / sizeof(anim_messages[0]))

static lv_color_t pct_color(float pct) {
    if (pct >= 80.0f) return COL_RED;
    if (pct >= 50.0f) return COL_AMBER;
    return COL_GREEN;
}

static void format_reset_time(int mins, char* buf, size_t len) {
    if (mins < 0) {
        snprintf(buf, len, "---");
    } else if (mins < 60) {
        snprintf(buf, len, "Resets in %dm", mins);
    } else if (mins < 1440) {
        snprintf(buf, len, "Resets in %dh %dm", mins / 60, mins % 60);
    } else {
        snprintf(buf, len, "Resets in %dd %dh", mins / 1440, (mins % 1440) / 60);
    }
}

static void global_click_cb(lv_event_t* e);
static void wifi_reset_click_cb(lv_event_t* e);

static lv_obj_t* make_panel(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 6, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_left(panel, 10, 0);
    lv_obj_set_style_pad_right(panel, 10, 0);
    lv_obj_set_style_pad_top(panel, 8, 0);
    lv_obj_set_style_pad_bottom(panel, 8, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_EVENT_BUBBLE);
    return panel;
}

static lv_obj_t* make_bar(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, w, h);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, COL_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, COL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
    return bar;
}

// One Session/Weekly panel: big % label, pill on the right, bar, reset label.
// Panel outer size = CONTENT_W × PANEL_H; pad 10/8 gives inner 280×64 px.
static void make_usage_panel(lv_obj_t* parent, int y, const char* pill_text,
                              lv_obj_t** out_pct, lv_obj_t** out_bar,
                              lv_obj_t** out_reset) {
    lv_obj_t* panel = make_panel(parent, MARGIN, y, CONTENT_W, PANEL_H);

    *out_pct = lv_label_create(panel);
    lv_label_set_text(*out_pct, "---%");
    lv_obj_set_style_text_font(*out_pct, &font_styrene_28, 0);
    lv_obj_set_style_text_color(*out_pct, COL_TEXT, 0);
    lv_obj_set_pos(*out_pct, 0, 0);

    // Pill label (top-right)
    lv_obj_t* pill = lv_label_create(panel);
    lv_label_set_text(pill, pill_text);
    lv_obj_set_style_text_font(pill, &font_styrene_12, 0);
    lv_obj_set_style_text_color(pill, COL_TEXT, 0);
    lv_obj_set_style_bg_color(pill, COL_BAR_BG, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_left(pill, 8, 0);
    lv_obj_set_style_pad_right(pill, 8, 0);
    lv_obj_set_style_pad_top(pill, 3, 0);
    lv_obj_set_style_pad_bottom(pill, 3, 0);
    lv_obj_align(pill, LV_ALIGN_TOP_RIGHT, 0, 2);

    // Bar: inner width = CONTENT_W - 2×pad_left = 300 - 20 = 280
    *out_bar = make_bar(panel, 0, 32, CONTENT_W - 20, 8);

    *out_reset = lv_label_create(panel);
    lv_label_set_text(*out_reset, "---");
    lv_obj_set_style_text_font(*out_reset, &font_styrene_12, 0);
    lv_obj_set_style_text_color(*out_reset, COL_DIM, 0);
    lv_obj_set_pos(*out_reset, 0, 46);
}

// ======== Usage Screen (320×240) ========

static void init_usage_screen(lv_obj_t* scr) {
    usage_container = lv_obj_create(scr);
    lv_obj_set_size(usage_container, SCR_W, SCR_H);
    lv_obj_set_pos(usage_container, 0, 0);
    lv_obj_set_style_bg_opa(usage_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(usage_container, 0, 0);
    lv_obj_set_style_pad_all(usage_container, 0, 0);
    lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(usage_container, global_click_cb, LV_EVENT_CLICKED, NULL);

    lbl_title = lv_label_create(usage_container);
    lv_label_set_text(lbl_title, "Usage");
    lv_obj_set_style_text_font(lbl_title, &font_tiempos_34, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, TITLE_Y);

    make_usage_panel(usage_container, CONTENT_Y, "Session",
                     &lbl_session_pct, &bar_session, &lbl_session_reset);
    make_usage_panel(usage_container, CONTENT_Y + PANEL_H + PANEL_GAP, "Weekly",
                     &lbl_weekly_pct, &bar_weekly, &lbl_weekly_reset);

    lbl_anim = lv_label_create(usage_container);
    lv_label_set_text(lbl_anim, "");
    lv_obj_set_style_text_font(lbl_anim, &font_mono_18, 0);
    lv_obj_set_style_text_color(lbl_anim, COL_ACCENT, 0);
    lv_obj_align(lbl_anim, LV_ALIGN_BOTTOM_MID, 0, -8);
}

// ======== WiFi Screen (320×240) ========

static void init_wifi_screen(lv_obj_t* scr) {
    wifi_container = lv_obj_create(scr);
    lv_obj_set_size(wifi_container, SCR_W, SCR_H);
    lv_obj_set_pos(wifi_container, 0, 0);
    lv_obj_set_style_bg_opa(wifi_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_container, 0, 0);
    lv_obj_set_style_pad_all(wifi_container, 0, 0);
    lv_obj_clear_flag(wifi_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl_wifi_title = lv_label_create(wifi_container);
    lv_label_set_text(lbl_wifi_title, "WiFi");
    lv_obj_set_style_text_font(lbl_wifi_title, &font_tiempos_34, 0);
    lv_obj_set_style_text_color(lbl_wifi_title, COL_TEXT, 0);
    lv_obj_align(lbl_wifi_title, LV_ALIGN_TOP_MID, 0, TITLE_Y);

    // Info panel: MQTT status + connection details
    lv_obj_t* p_info = make_panel(wifi_container, MARGIN, CONTENT_Y, CONTENT_W, 90);

    lbl_wifi_status = lv_label_create(p_info);
    lv_label_set_text(lbl_wifi_status, "Connecting...");
    lv_obj_set_style_text_font(lbl_wifi_status, &font_styrene_16, 0);
    lv_obj_set_style_text_color(lbl_wifi_status, COL_DIM, 0);
    lv_obj_set_pos(lbl_wifi_status, 0, 0);

    lbl_wifi_ssid = lv_label_create(p_info);
    lv_label_set_text(lbl_wifi_ssid, "SSID: ---");
    lv_obj_set_style_text_font(lbl_wifi_ssid, &font_styrene_12, 0);
    lv_obj_set_style_text_color(lbl_wifi_ssid, COL_DIM, 0);
    lv_obj_set_pos(lbl_wifi_ssid, 0, 22);

    lbl_wifi_ip = lv_label_create(p_info);
    lv_label_set_text(lbl_wifi_ip, "IP: ---");
    lv_obj_set_style_text_font(lbl_wifi_ip, &font_styrene_12, 0);
    lv_obj_set_style_text_color(lbl_wifi_ip, COL_DIM, 0);
    lv_obj_set_pos(lbl_wifi_ip, 0, 38);

    lbl_wifi_broker = lv_label_create(p_info);
    lv_label_set_text(lbl_wifi_broker, "Broker: ---");
    lv_obj_set_style_text_font(lbl_wifi_broker, &font_styrene_12, 0);
    lv_obj_set_style_text_color(lbl_wifi_broker, COL_DIM, 0);
    lv_obj_set_pos(lbl_wifi_broker, 0, 54);

    // Reset WiFi button
    int reset_y = CONTENT_Y + 90 + PANEL_GAP;
    lv_obj_t* reset_zone = lv_obj_create(wifi_container);
    lv_obj_set_pos(reset_zone, MARGIN, reset_y);
    lv_obj_set_size(reset_zone, CONTENT_W, 46);
    lv_obj_set_style_bg_color(reset_zone, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(reset_zone, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(reset_zone, 6, 0);
    lv_obj_set_style_border_width(reset_zone, 0, 0);
    lv_obj_set_flex_flow(reset_zone, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(reset_zone, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(reset_zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(reset_zone, wifi_reset_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* reset_lbl = lv_label_create(reset_zone);
    lv_label_set_text(reset_lbl, "Reset WiFi Credentials");
    lv_obj_set_style_text_font(reset_lbl, &font_styrene_14, 0);
    lv_obj_set_style_text_color(reset_lbl, COL_DIM, 0);

    // Attribution
    lv_obj_t* lbl_credit = lv_label_create(wifi_container);
    lv_label_set_text(lbl_credit, "Built by @hermannbjorgvin");
    lv_obj_set_style_text_font(lbl_credit, &font_styrene_12, 0);
    lv_obj_set_style_text_color(lbl_credit, COL_DIM, 0);
    lv_obj_align(lbl_credit, LV_ALIGN_BOTTOM_MID, 0, -16);

    lv_obj_t* lbl_credit2 = lv_label_create(wifi_container);
    lv_label_set_text(lbl_credit2, "Clawd animation by @amaanbuilds");
    lv_obj_set_style_text_font(lbl_credit2, &font_styrene_12, 0);
    lv_obj_set_style_text_color(lbl_credit2, COL_DIM, 0);
    lv_obj_align(lbl_credit2, LV_ALIGN_BOTTOM_MID, 0, -4);

    lv_obj_add_flag(wifi_container, LV_OBJ_FLAG_HIDDEN);
}

// ======== Public API ========

void ui_init(void) {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    init_usage_screen(scr);
    init_wifi_screen(scr);
    splash_init(scr);

    if (splash_get_root()) {
        lv_obj_add_event_cb(splash_get_root(), global_click_cb, LV_EVENT_CLICKED, NULL);
    }
}

void ui_update(const UsageData* data) {
    if (!data->valid) return;

    int s_pct = (int)(data->session_pct + 0.5f);
    lv_label_set_text_fmt(lbl_session_pct, "%d%%", s_pct);
    lv_bar_set_value(bar_session, s_pct, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar_session, pct_color(data->session_pct), LV_PART_INDICATOR);

    char buf[48];
    format_reset_time(data->session_reset_mins, buf, sizeof(buf));
    lv_label_set_text(lbl_session_reset, buf);

    int w_pct = (int)(data->weekly_pct + 0.5f);
    lv_label_set_text_fmt(lbl_weekly_pct, "%d%%", w_pct);
    lv_bar_set_value(bar_weekly, w_pct, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar_weekly, pct_color(data->weekly_pct), LV_PART_INDICATOR);

    format_reset_time(data->weekly_reset_mins, buf, sizeof(buf));
    lv_label_set_text(lbl_weekly_reset, buf);
}

void ui_tick_anim(void) {
    if (current_screen != SCREEN_USAGE) return;

    uint32_t now = lv_tick_get();

    if (now - anim_msg_start >= ANIM_MSG_MS) {
        anim_msg_idx = (anim_msg_idx + 1) % ANIM_MSG_COUNT;
        anim_msg_start = now;
    }

    if (now - anim_last_ms >= spinner_ms[anim_spinner_idx]) {
        anim_last_ms = now;
        anim_phase = (anim_phase + 1) % SPINNER_PHASES;
        anim_spinner_idx = (anim_phase < SPINNER_COUNT) ? anim_phase
                                                        : (SPINNER_PHASES - anim_phase);
        static char buf[80];
        snprintf(buf, sizeof(buf), "%s %s\xE2\x80\xA6",
                 spinner_frames[anim_spinner_idx],
                 anim_messages[anim_msg_idx]);
        lv_label_set_text(lbl_anim, buf);
    }
}

static screen_t prev_non_splash_screen = SCREEN_USAGE;

static void global_click_cb(lv_event_t* e) {
    (void)e;
    if (ui_get_current_screen() == SCREEN_WIFI) return;
    ui_toggle_splash();
}

static void wifi_reset_click_cb(lv_event_t* e) {
    (void)e;
    wifi_mqtt_start_config_portal();
}

void ui_show_screen(screen_t screen) {
    lv_obj_add_flag(usage_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifi_container, LV_OBJ_FLAG_HIDDEN);
    splash_hide();

    switch (screen) {
    case SCREEN_SPLASH:  splash_show(); break;
    case SCREEN_USAGE:   lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_WIFI:    lv_obj_clear_flag(wifi_container, LV_OBJ_FLAG_HIDDEN); break;
    default: break;
    }

    if (screen != SCREEN_SPLASH) prev_non_splash_screen = screen;
    current_screen = screen;
}

void ui_cycle_screen(void) {
    screen_t next = (current_screen == SCREEN_USAGE) ? SCREEN_WIFI : SCREEN_USAGE;
    ui_show_screen(next);
}

void ui_toggle_splash(void) {
    if (current_screen == SCREEN_SPLASH) ui_show_screen(prev_non_splash_screen);
    else                                  ui_show_screen(SCREEN_SPLASH);
}

screen_t ui_get_current_screen(void) { return current_screen; }

void ui_update_wifi_status(mqtt_state_t state, const char* ssid,
                            const char* ip, int rssi, const char* broker) {
    switch (state) {
    case MQTT_STATE_CONNECTED:
        lv_label_set_text(lbl_wifi_status, "MQTT: Connected");
        lv_obj_set_style_text_color(lbl_wifi_status, COL_GREEN, 0);
        break;
    case MQTT_STATE_CONNECTING:
        lv_label_set_text(lbl_wifi_status, "MQTT: Connecting...");
        lv_obj_set_style_text_color(lbl_wifi_status, COL_AMBER, 0);
        break;
    default:
        lv_label_set_text(lbl_wifi_status, "MQTT: Disconnected");
        lv_obj_set_style_text_color(lbl_wifi_status, COL_RED, 0);
        break;
    }

    if (ssid) {
        static char sbuf[48];
        snprintf(sbuf, sizeof(sbuf), "SSID: %s  (%d dBm)", ssid, rssi);
        lv_label_set_text(lbl_wifi_ssid, sbuf);
    }
    if (ip) {
        static char ibuf[48];
        snprintf(ibuf, sizeof(ibuf), "IP: %s", ip);
        lv_label_set_text(lbl_wifi_ip, ibuf);
    }
    if (broker) {
        static char bbuf[80];
        snprintf(bbuf, sizeof(bbuf), "Broker: %s", broker);
        lv_label_set_text(lbl_wifi_broker, bbuf);
    }
}

// CYD is USB-powered — battery display not applicable.
void ui_update_battery(int /*percent*/, bool /*charging*/) {}
