#include "wifi_mqtt.h"
#include "display_cfg.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <Preferences.h>

#define TOPIC_USAGE    "clawdmeter/usage"
#define TOPIC_REQUEST  "clawdmeter/request"
#define MQTT_BUF_SIZE  512
#define RECONNECT_MS   5000
#ifndef MQTT_DEFAULT_BROKER
#define MQTT_DEFAULT_BROKER "192.168.1.100"
#endif
#ifndef MQTT_DEFAULT_USER
#define MQTT_DEFAULT_USER ""
#endif
#ifndef MQTT_DEFAULT_PASS
#define MQTT_DEFAULT_PASS ""
#endif
#define DEFAULT_PORT 1883

static WiFiClient   wifi_client;
static PubSubClient mqtt(wifi_client);
static WiFiManager  wm;
static Preferences  prefs;

static char     broker_ip[64]   = MQTT_DEFAULT_BROKER;
static uint16_t broker_port     = DEFAULT_PORT;
static char     mqtt_user[64]   = "";
static char     mqtt_pass[128]  = "";
static char     mqtt_buf[MQTT_BUF_SIZE];
static bool     data_ready      = false;
static mqtt_state_t state       = MQTT_STATE_DISCONNECTED;
static uint32_t last_reconnect  = 0;

// Pointers to WiFiManager custom parameters so save_params_cb can read them.
static WiFiManagerParameter* p_broker_param = nullptr;
static WiFiManagerParameter* p_user_param   = nullptr;
static WiFiManagerParameter* p_pass_param   = nullptr;

static void save_params_cb() {
    if (p_broker_param) {
        strlcpy(broker_ip, p_broker_param->getValue(), sizeof(broker_ip));
        prefs.putString("broker", broker_ip);
    }
    if (p_user_param) {
        strlcpy(mqtt_user, p_user_param->getValue(), sizeof(mqtt_user));
        prefs.putString("mqtt_user", mqtt_user);
    }
    if (p_pass_param) {
        strlcpy(mqtt_pass, p_pass_param->getValue(), sizeof(mqtt_pass));
        prefs.putString("mqtt_pass", mqtt_pass);
    }
    Serial.printf("MQTT saved: broker=%s user=%s\n", broker_ip, mqtt_user);
}

static void mqtt_callback(char* topic, byte* payload, unsigned int len) {
    if (len >= MQTT_BUF_SIZE) len = MQTT_BUF_SIZE - 1;
    memcpy(mqtt_buf, payload, len);
    mqtt_buf[len] = '\0';
    data_ready = true;
}

static bool mqtt_reconnect(void) {
    if (mqtt.connected()) { state = MQTT_STATE_CONNECTED; return true; }
    state = MQTT_STATE_CONNECTING;
    const char* user = (mqtt_user[0] != '\0') ? mqtt_user : nullptr;
    const char* pass = (mqtt_pass[0] != '\0') ? mqtt_pass : nullptr;
    if (mqtt.connect("ClawdmeterCYD", user, pass)) {
        mqtt.subscribe(TOPIC_USAGE);
        state = MQTT_STATE_CONNECTED;
        mqtt.publish(TOPIC_REQUEST, "refresh");
        Serial.println("MQTT: connected");
        return true;
    }
    state = MQTT_STATE_DISCONNECTED;
    Serial.printf("MQTT: connect failed (rc=%d)\n", mqtt.state());
    return false;
}

// Show a raw-GFX boot message while WiFiManager may be blocking.
// Called before LVGL is initialised, so we write directly to the GFX object.
static void draw_boot_screen(const char* line2) {
    gfx->fillScreen(0x0000);
    gfx->setTextColor(0xFFFF);
    gfx->setTextSize(2);
    gfx->setCursor(60, 95);
    gfx->print("Clawdmeter CYD");
    gfx->setTextSize(1);
    gfx->setTextColor(0xAD75);  // dim grey
    gfx->setCursor(0, 125);
    // centre the string approximately
    int cx = (320 - (int)strlen(line2) * 6) / 2;
    if (cx < 0) cx = 0;
    gfx->setCursor(cx, 130);
    gfx->print(line2);
}

void wifi_mqtt_init(void) {
    prefs.begin("clawdmeter", false);
    strlcpy(broker_ip,  prefs.getString("broker",    MQTT_DEFAULT_BROKER).c_str(), sizeof(broker_ip));
    strlcpy(mqtt_user,  prefs.getString("mqtt_user", MQTT_DEFAULT_USER).c_str(),   sizeof(mqtt_user));
    strlcpy(mqtt_pass,  prefs.getString("mqtt_pass", MQTT_DEFAULT_PASS).c_str(),   sizeof(mqtt_pass));
    // Fall back to compiled defaults if NVS has empty or placeholder values
    if (mqtt_user[0] == '\0') strlcpy(mqtt_user, MQTT_DEFAULT_USER, sizeof(mqtt_user));
    if (mqtt_pass[0] == '\0') strlcpy(mqtt_pass, MQTT_DEFAULT_PASS, sizeof(mqtt_pass));
    if (broker_ip[0] == '\0') strlcpy(broker_ip, MQTT_DEFAULT_BROKER, sizeof(broker_ip));
    Serial.printf("MQTT config: broker=%s user=%s pass=%s\n",
                  broker_ip, mqtt_user, mqtt_pass[0] ? "(set)" : "(empty)");
    broker_port = prefs.getUShort("port", DEFAULT_PORT);

    // Custom portal parameters
    static WiFiManagerParameter param_broker("broker", "MQTT Broker IP",  broker_ip, 63);
    static WiFiManagerParameter param_user  ("user",   "MQTT User",        mqtt_user, 63);
    static WiFiManagerParameter param_pass  ("pass",   "MQTT Password",    mqtt_pass, 127);
    p_broker_param = &param_broker;
    p_user_param   = &param_user;
    p_pass_param   = &param_pass;
    wm.addParameter(&param_broker);
    wm.addParameter(&param_user);
    wm.addParameter(&param_pass);
    wm.setConfigPortalTimeout(180);
    wm.setSaveParamsCallback(save_params_cb);
    wm.setSaveConfigCallback(save_params_cb);  // also fires on WiFi save page

    // Show what we're doing on the display before the potential blocking call
    draw_boot_screen("Connecting to WiFi...");

    wm.setAPCallback([](WiFiManager*) {
        draw_boot_screen("Open: Clawdmeter-Setup AP");
    });

    bool force_portal = prefs.getBool("force_portal", false);
    if (force_portal) {
        prefs.putBool("force_portal", false);
        draw_boot_screen("Config portal: Clawdmeter-Setup");
        wm.startConfigPortal("Clawdmeter-Setup");
    } else if (!wm.autoConnect("Clawdmeter-Setup")) {
        Serial.println("WiFi: portal timeout — restarting");
        ESP.restart();
    }

    Serial.printf("WiFi: connected SSID=%s IP=%s\n",
                  WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());

    mqtt.setServer(broker_ip, broker_port);
    mqtt.setCallback(mqtt_callback);
    mqtt_reconnect();
}

void wifi_mqtt_poll(void) {
    if (WiFi.status() != WL_CONNECTED) {
        state = MQTT_STATE_DISCONNECTED;
        return;
    }
    if (!mqtt.connected()) {
        uint32_t now = millis();
        if (now - last_reconnect >= RECONNECT_MS) {
            last_reconnect = now;
            mqtt_reconnect();
        }
        return;
    }
    mqtt.loop();
}

mqtt_state_t wifi_mqtt_get_state(void)  { return state; }
const char*  wifi_mqtt_get_ssid(void)   { return WiFi.SSID().c_str(); }
const char*  wifi_mqtt_get_ip(void)     { return WiFi.localIP().toString().c_str(); }
int          wifi_mqtt_get_rssi(void)   { return WiFi.RSSI(); }
const char*  wifi_mqtt_get_broker(void) { return broker_ip; }
bool         wifi_mqtt_has_data(void)   { return data_ready; }

const char* wifi_mqtt_get_data(void) {
    data_ready = false;
    return mqtt_buf;
}

void wifi_mqtt_start_config_portal(void) {
    prefs.putBool("force_portal", true);
    ESP.restart();
}
