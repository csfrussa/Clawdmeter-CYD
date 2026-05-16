#include "power.h"
#include <Arduino.h>

// Middle button (GPIO 0 — boot button on ESP32 CYD).
// Debounced by polling at 50 ms intervals; sets a one-shot flag on falling edge.
#define BTN_MID       0
#define PWR_POLL_MS  50

static bool     pwr_pressed_flag   = false;
static bool     pwr_long_held_flag = false;
static bool     mid_was            = false;
static uint32_t last_poll_ms       = 0;
static uint32_t mid_held_since     = 0;

void power_init(void) {
    pinMode(BTN_MID, INPUT_PULLUP);
}

void power_tick(void) {
    uint32_t now = millis();
    if (now - last_poll_ms < PWR_POLL_MS) return;
    last_poll_ms = now;

    bool mid_now = (digitalRead(BTN_MID) == LOW);
    if (mid_now && !mid_was) {
        mid_held_since = now;
    }
    if (mid_now) {
        if (!pwr_long_held_flag && (now - mid_held_since >= 5000)) {
            pwr_long_held_flag = true;
        }
    } else {
        if (mid_was && (now - mid_held_since < 5000)) pwr_pressed_flag = true;
        mid_held_since = 0;
        pwr_long_held_flag = false;
    }
    mid_was = mid_now;
}

// CYD is USB-powered — no battery measurement available.
int  power_battery_pct(void)  { return 100; }
bool power_is_charging(void)  { return true; }

bool power_pwr_pressed(void) {
    if (pwr_pressed_flag) { pwr_pressed_flag = false; return true; }
    return false;
}

bool power_pwr_long_held(void) {
    if (pwr_long_held_flag) { pwr_long_held_flag = false; return true; }
    return false;
}
