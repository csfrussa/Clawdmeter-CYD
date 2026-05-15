#include <Arduino.h>
#include <lvgl.h>
#include <ArduinoJson.h>
#include "display_cfg.h"
#include "data.h"
#include "ui.h"
#include "ble.h"
#include "power.h"
#include "imu.h"
#include "splash.h"
#include "usage_rate.h"
#include "wifi_mqtt.h"

// Physical buttons (screen-independent):
//   BTN_LEFT  (GPIO 35) → Space        (Claude Code voice-mode push-to-talk)
//   BTN_RIGHT (GPIO 34) → Shift+Tab    (Claude Code mode toggle)
//   BTN_MID   (GPIO  0) → cycle screens; on splash, cycle animations (power.cpp)

// ---- Hardware objects ----
Arduino_DataBus* bus = new Arduino_ESP32SPI(
    LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, LCD_MISO);
Arduino_ILI9341* gfx = new Arduino_ILI9341(bus, LCD_RST, 1 /* landscape */);

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

static UsageData usage = {};

// ---- Touch shared state (read once per loop, consumed by LVGL + touch.cpp) ----
static bool     touch_pressed = false;
static uint16_t touch_x = 0;
static uint16_t touch_y = 0;

static void touch_read() {
    if (!touch.touched()) {
        touch_pressed = false;
        return;
    }
    TS_Point tp = touch.getPoint();

    // Map raw XPT2046 values to screen coordinates (landscape, rotation=1).
    // Raw X/Y axes on CYD are swapped relative to screen X/Y in landscape mode;
    // raw Y is also mirrored. Adjust TOUCH_*_MIN/MAX in display_cfg.h if needed.
    int x = map(tp.y, TOUCH_Y_MIN, TOUCH_Y_MAX, LCD_WIDTH  - 1, 0);
    int y = map(tp.x, TOUCH_X_MIN, TOUCH_X_MAX, 0,          LCD_HEIGHT - 1);
    x = constrain(x, 0, LCD_WIDTH  - 1);
    y = constrain(y, 0, LCD_HEIGHT - 1);

    touch_pressed = true;
    touch_x = (uint16_t)x;
    touch_y = (uint16_t)y;
}

// ---- LVGL draw buffers (static SRAM — no PSRAM on CYD) ----
#define BUF_LINES 10
static uint16_t buf1[LCD_WIDTH * BUF_LINES];
static uint16_t buf2[LCD_WIDTH * BUF_LINES];

static uint32_t my_tick(void) { return millis(); }

static void my_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)px_map, w, h);
    lv_display_flush_ready(disp);
}

static void my_touch_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    if (touch_pressed) {
        data->point.x = touch_x;
        data->point.y = touch_y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static bool parse_json(const char* json, UsageData* out) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("JSON parse error: %s\n", err.c_str());
        return false;
    }
    out->session_pct        = doc["s"]  | 0.0f;
    out->session_reset_mins = doc["sr"] | -1;
    out->weekly_pct         = doc["w"]  | 0.0f;
    out->weekly_reset_mins  = doc["wr"] | -1;
    strlcpy(out->status, doc["st"] | "unknown", sizeof(out->status));
    out->ok    = doc["ok"] | false;
    out->valid = true;
    return true;
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("{\"ready\":true}");

    // Disable RGB LED (active LOW, common anode)
    pinMode(LED_R, OUTPUT); digitalWrite(LED_R, HIGH);
    pinMode(LED_G, OUTPUT); digitalWrite(LED_G, HIGH);
    pinMode(LED_B, OUTPUT); digitalWrite(LED_B, HIGH);

    // Init display (raw GFX — used for boot screen before LVGL)
    gfx->begin();
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, HIGH);
    gfx->fillScreen(0x0000);

    // Init peripherals
    power_init();
    imu_init();

    // Init XPT2046 touch on separate VSPI bus
    touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    touch.begin(touchSPI);
    touch.setRotation(1);

    // Init BLE HID keyboard
    ble_init();

    // Init WiFi + MQTT — may block while WiFiManager portal is open.
    // Draws raw-GFX boot screen internally.
    wifi_mqtt_init();

    // Init LVGL
    lv_init();
    lv_tick_set_cb(my_tick);

    lv_display_t* disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, my_flush_cb);
    lv_display_set_buffers(disp, buf1, buf2, sizeof(buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touch_cb);

    // Physical left/right buttons
    pinMode(BTN_LEFT,  INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);

    // Build dashboard
    ui_init();

    // Show initial WiFi/MQTT state
    ui_update_wifi_status(wifi_mqtt_get_state(),
                          wifi_mqtt_get_ssid(), wifi_mqtt_get_ip(),
                          wifi_mqtt_get_rssi(), wifi_mqtt_get_broker());

    ui_show_screen(SCREEN_SPLASH);

    Serial.println("Dashboard ready");
}

static mqtt_state_t last_mqtt_state = MQTT_STATE_DISCONNECTED;
static uint32_t     last_wifi_ui_ms = 0;

void loop() {
    touch_read();
    lv_timer_handler();
    ui_tick_anim();
    ble_tick();
    power_tick();
    imu_tick();
    splash_tick();
    wifi_mqtt_poll();

    // Left button → Space (voice-mode push-to-talk)
    // Right button → Shift+Tab (mode toggle)
    {
        static bool left_was = false, right_was = false;
        bool left_now  = (digitalRead(BTN_LEFT)  == LOW);
        bool right_now = (digitalRead(BTN_RIGHT) == LOW);

        if (left_now != left_was) {
            if (left_now) ble_keyboard_press(0x2C, 0);      // HID Space
            else          ble_keyboard_release();
            left_was = left_now;
        }
        if (right_now != right_was) {
            if (right_now) ble_keyboard_press(0x2B, 0x02);  // HID Tab + LEFT_SHIFT
            else           ble_keyboard_release();
            right_was = right_now;
        }

        if (power_pwr_pressed()) {
            if (ui_get_current_screen() == SCREEN_SPLASH) splash_next();
            else                                          ui_cycle_screen();
        }
    }

    // Update WiFi/MQTT status on screen — on state change or every 10 s
    {
        mqtt_state_t ms = wifi_mqtt_get_state();
        uint32_t now = millis();
        if (ms != last_mqtt_state || now - last_wifi_ui_ms >= 10000) {
            last_mqtt_state = ms;
            last_wifi_ui_ms = now;
            ui_update_wifi_status(ms, wifi_mqtt_get_ssid(), wifi_mqtt_get_ip(),
                                  wifi_mqtt_get_rssi(), wifi_mqtt_get_broker());
        }
    }

    // Process incoming MQTT data
    if (wifi_mqtt_has_data()) {
        if (parse_json(wifi_mqtt_get_data(), &usage)) {
            int g_before = usage_rate_group();
            usage_rate_sample(usage.session_pct);
            int g_after = usage_rate_group();
            if (g_after != g_before) {
                Serial.printf("usage rate: group %d -> %d (s=%.2f%%)\n",
                    g_before, g_after, usage.session_pct);
                if (splash_is_active()) splash_pick_for_current_rate();
            }
            ui_update(&usage);
        }
    }

    delay(5);
}
