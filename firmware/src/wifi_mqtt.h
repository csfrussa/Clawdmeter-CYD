#pragma once
#include <stdint.h>
#include <stdbool.h>

enum mqtt_state_t {
    MQTT_STATE_DISCONNECTED,
    MQTT_STATE_CONNECTING,
    MQTT_STATE_CONNECTED,
};

// Call once in setup() — blocks while WiFiManager portal is open if no
// credentials are saved in flash. Updates display via raw GFX before LVGL init.
void wifi_mqtt_init(void);

// Call every loop iteration to service the MQTT client.
void wifi_mqtt_poll(void);

// Status queries
mqtt_state_t wifi_mqtt_get_state(void);
const char*  wifi_mqtt_get_ssid(void);
const char*  wifi_mqtt_get_ip(void);
int          wifi_mqtt_get_rssi(void);
const char*  wifi_mqtt_get_broker(void);

// Incoming data (published by daemon to clawdmeter/usage)
bool        wifi_mqtt_has_data(void);
const char* wifi_mqtt_get_data(void);

// Wipe saved WiFi credentials and reboot into config portal.
void wifi_mqtt_start_config_portal(void);
