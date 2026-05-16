// BLE disabled on CYD build — not enough DRAM alongside WiFi stack.
// All symbols are stubbed so ble.h interface remains intact.
#include "ble.h"

void        ble_init(void)                              {}
void        ble_tick(void)                              {}
ble_state_t ble_get_state(void)                         { return BLE_STATE_INIT; }
const char* ble_get_device_name(void)                   { return ""; }
const char* ble_get_mac_address(void)                   { return ""; }
void        ble_clear_bonds(void)                       {}
void        ble_keyboard_press(uint8_t, uint8_t)        {}
void        ble_keyboard_release(void)                  {}
