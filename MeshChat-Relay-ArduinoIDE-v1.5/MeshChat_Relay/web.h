// Wi-Fi access point + web server: chat page at "/", setup page at "/setup" (password protected).
#pragma once
#include "bt_keyboard.h"
#include <Arduino.h>
#include "mc_node.h"
#include "settings.h"
#include "ble_link.h"
#include "lora_radio.h"
#include "weather.h"

class WebUi {
public:
    void begin(Settings* s, mc::RelayNode* node, BleLink* ble, LoraRadio* lora, WeatherStation* wx, BtKeyboard* kb);
    void handle();
    String ip() const;
};
