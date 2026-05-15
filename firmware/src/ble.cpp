#include "ble.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

#define DEVICE_NAME "Claude Controller"

// HID keyboard report descriptor (8-key NKRO layout)
static const uint8_t HID_REPORT_MAP[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)
    0x85, 0x01,  //   Report ID (1)
    0x05, 0x07,  //   Usage Page (Key Codes)
    0x19, 0xE0,  //   Usage Minimum (224) - Left Control
    0x29, 0xE7,  //   Usage Maximum (231) - Right GUI
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8)
    0x81, 0x02,  //   Input (Data, Variable, Absolute) - Modifier byte
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x08,  //   Report Size (8)
    0x81, 0x01,  //   Input (Constant) - Reserved byte
    0x95, 0x06,  //   Report Count (6)
    0x75, 0x08,  //   Report Size (8)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x65,  //   Logical Maximum (101)
    0x05, 0x07,  //   Usage Page (Key Codes)
    0x19, 0x00,  //   Usage Minimum (0)
    0x29, 0x65,  //   Usage Maximum (101)
    0x81, 0x00,  //   Input (Data, Array) - Key array (6 keys)
    0xC0,        // End Collection
};

static NimBLEServer*      server    = nullptr;
static NimBLEHIDDevice*   hid_dev   = nullptr;
static NimBLECharacteristic* input_kbd = nullptr;

static ble_state_t state        = BLE_STATE_INIT;
static bool        need_advertise = false;
static char        mac_str[18];

static void start_advertising() {
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->reset();
    adv->setAppearance(HID_KEYBOARD);
    adv->enableScanResponse(true);
    adv->setName(DEVICE_NAME);
    bool ok = adv->start();
    state = BLE_STATE_ADVERTISING;
    Serial.printf("BLE: advertising start=%s\n", ok ? "OK" : "FAILED");
}

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
        state = BLE_STATE_CONNECTED;
        Serial.printf("BLE: connected from %s\n", info.getAddress().toString().c_str());
    }
    void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
        state = BLE_STATE_DISCONNECTED;
        need_advertise = true;
        Serial.printf("BLE: disconnected (reason=%d)\n", reason);
    }
};

void ble_init(void) {
    NimBLEDevice::init(DEVICE_NAME);
    NimBLEDevice::setSecurityAuth(true, false, true);  // bonding, no MITM, SC

    NimBLEAddress addr = NimBLEDevice::getAddress();
    snprintf(mac_str, sizeof(mac_str), "%s", addr.toString().c_str());
    for (int i = 0; mac_str[i]; i++) {
        if (mac_str[i] >= 'a' && mac_str[i] <= 'f') mac_str[i] -= 32;
    }

    server = NimBLEDevice::createServer();
    static ServerCallbacks serverCb;
    server->setCallbacks(&serverCb);

    hid_dev = new NimBLEHIDDevice(server);
    hid_dev->setReportMap((uint8_t*)HID_REPORT_MAP, sizeof(HID_REPORT_MAP));
    hid_dev->setManufacturer("Anthropic");
    hid_dev->setPnp(0x02, 0x05AC, 0x820A, 0x0210);
    hid_dev->setHidInfo(0x00, 0x02);
    hid_dev->setBatteryLevel(100);
    input_kbd = hid_dev->getInputReport(1);

    server->start();
    start_advertising();

    Serial.printf("BLE: HID keyboard ready, MAC=%s\n", mac_str);
}

void ble_tick(void) {
    if (need_advertise) {
        need_advertise = false;
        start_advertising();
    }
}

ble_state_t ble_get_state(void)       { return state; }
const char* ble_get_device_name(void) { return DEVICE_NAME; }
const char* ble_get_mac_address(void) { return mac_str; }

void ble_clear_bonds(void) {
    NimBLEDevice::deleteAllBonds();
    Serial.println("BLE: bonds cleared");
    if (state == BLE_STATE_CONNECTED) {
        server->disconnect(server->getPeerInfo(0).getConnHandle());
    }
    need_advertise = true;
}

void ble_keyboard_press(uint8_t key, uint8_t modifier) {
    if (state != BLE_STATE_CONNECTED || !input_kbd) return;
    uint8_t report[8] = {modifier, 0, key, 0, 0, 0, 0, 0};
    input_kbd->setValue(report, sizeof(report));
    input_kbd->notify();
}

void ble_keyboard_release(void) {
    if (state != BLE_STATE_CONNECTED || !input_kbd) return;
    uint8_t report[8] = {0};
    input_kbd->setValue(report, sizeof(report));
    input_kbd->notify();
}
