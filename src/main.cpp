// MeshChat LoRa relay node.
//   phones  <--BLE-->  [ESP32 relay]  <--LoRa-->  [ESP32 relay]  <--BLE-->  phones
// plus a chat of its own: keyboard + 1602 LCD, and a Wi-Fi hotspot with a web chat and a setup page.
#include <Arduino.h>
#include "config.h"
#include "settings.h"
#include "mc_node.h"
#include "ble_link.h"
#include "lora_radio.h"
#include "lcd.h"
#include "keyboard.h"
#include "ui.h"
#include "web.h"
#include "alerts.h"
#include "weather.h"

static Settings settings;
static mc::crypto::Identity identity;
static mc::RelayNode* node = nullptr;
static BleLink ble;
static LoraRadio lora;
static Lcd lcd;
static Keyboard keyboard;
static ChatUi ui;
static WebUi web;
static Alerts alerts;
static WeatherStation weather;

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\nMeshChat relay " FIRMWARE_VERSION);
    settingsLoad(settings);
    lcd.begin(settings.lcdAddr);
    lcd.show("MeshChat relay", "starting...");

    if (!identityLoad(identity)) {
        lcd.show("KEY ERROR", "restart device");
        for (;;) delay(1000);
    }
    Serial.printf("node id %s\n", mc::idLabel(identity.nodeId()).c_str());

    mc::NodeConfig cfg;
    cfg.name = settings.name.c_str();
    cfg.loraEnabled = settings.loraOn;
    cfg.loraSf = settings.sf;
    cfg.loraBwHz = int(settings.bw * 1000);
    cfg.loraCr = settings.cr;
    cfg.dutyPercent = settings.duty;
    cfg.identityBeaconMs = uint32_t(settings.beaconMin) * 60000UL;
    cfg.loraMaxTtl = settings.loraTtl;

    mc::RelayNode::Io io;
    io.bleSend = [](uint32_t peer, const mc::Bytes& b) { return ble.send(peer, b); };
    io.loraSend = [](const mc::Bytes& f) { return lora.transmit(f); };
    io.loraBusy = []() { return lora.channelBusy(); };      // listen before talk
    io.log = [](const std::string& s) { Serial.println(s.c_str()); };
    node = new mc::RelayNode(identity, cfg, io);

    ble.onPacket = [](uint32_t peer, const mc::Bytes& p) { node->onBleFrame(peer, p); };
    ble.onDown = [](uint32_t peer) { node->onBleLinkDown(peer); };
    ble.begin(identity.nodeId(), cfg.caps);

    bool loraOk = false;
    if (settings.loraOn) {
        lora.onFrame = [](const mc::Bytes& f, float, float) { node->onLoraFrame(f); };
        loraOk = lora.begin(settings);
    }
    web.begin(&settings, node, &ble, &lora, &weather);
    keyboard.begin();
    ui.begin(node, &lcd, &settings, web.ip(), loraOk || !settings.loraOn);
    alerts.begin(settings);
    weather.begin(&settings);                 // after lcd.begin(): the BMP280 shares its I2C bus
    Serial.println("ready");
}

void loop() {
    uint32_t now = millis();
    ble.poll(now);
    lora.poll();
    web.handle();
    mc::KeyEvent ev;
    while (keyboard.next(ev)) { ui.key(ev); alerts.acknowledge(); }
    alerts.poll(node, (int)ble.links(), now);
    weather.poll(node, now);
    node->tick(now);
    ui.tick(now);
    delay(2);
}
