#pragma once

void power_init(void);
void power_tick(void);
int  power_battery_pct(void);    // 0-100, or -1 if no battery
bool power_is_charging(void);
bool power_pwr_pressed(void);    // true once per short-press
bool power_pwr_long_held(void);  // true once after 5 s hold
