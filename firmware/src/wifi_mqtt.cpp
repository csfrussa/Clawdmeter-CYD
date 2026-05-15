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
#define DEFAULT_BROKER "192.168.1.100"
#define DEFAULT_PORT   1883

static WiFiClient   wifi_client;
static PubSubClient mqtt(wifi_client);
static WiFiManager  wm;
static Preferences  prefs;

static char     broker_ip[64]  = DEFAULT_BROKER;
static uint16_t broker_port    = DEFAULT_PORT;
static char     mqtt_buf[MQTT_BUF_SIZE];
static bool     data_ready     = false;
static mqtt_state_t state      = MQTT_STATE_DISCONNECTED;
static uint32_t last_reconnect = 0;

// Pointer to the WiFiManager custom parameter so save_params_cb can read it.
static WiFiManagerParameter* p_broker_param = nullptr;

static void save_params_cb() {
    if (!p_broker_param) return;
    strlcpy(broker_ip, p_broker_param->getValue(), sizeof(broker_ip));
    prefs.putString("broker", broker_ip);
    Serial.printf("MQTT broker saved: %s\n", broker_ip);
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
    if (mqtt.connect("ClawdmeterCYD")) {
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
    strlcpy(broker_ip, prefs.getString("broker", DEFAULT_BROKER).c_str(), sizeof(broker_ip));
    broker_port = prefs.getUShort("port", DEFAULT_PORT);

    // Custom portal parameter for MQTT broker IP
    static WiFiManagerParameter param_broker("broker", "MQTT Broker IP", broker_ip, 63);
    p_broker_param = &param_broker;
    wm.addParameter(&param_broker);
    wm.setConfigPortalTimeout(180);
    wm.setSaveParamsCallback(save_params_cb);

    // Show what we're doing on the display before the potential blocking call
    draw_boot_screen("Connecting to WiFi...");

    wm.setAPCallback([](WiFiManager*) {
        draw_boot_screen("Open: Clawdmeter-Setup AP");
    });

    if (!wm.autoConnect("Clawdmeter-Setup")) {
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
    wm.resetSettings();
    ESP.restart();
}
