// Indicator LED + small speaker/buzzer. What happens when is decided in lib/meshcore/mc_alert.* (tested on a PC).
#pragma once
#include <Arduino.h>
#include "mc_alert.h"
#include "mc_node.h"
#include "settings.h"

class BleLink;

class Alerts {
public:
    void begin(const Settings& s);
    void poll(mc::RelayNode* node, int bleLinks, uint32_t now);
    void acknowledge() { player_->acknowledge(); }     // any key press on the keyboard
private:
    mc::AlertPlayer* player_ = nullptr;
    int ledPin_ = -1, buzPin_ = -1;
    uint32_t lastSeq_ = 0, rx_ = 0, tx_ = 0;
    int links_ = 0;
};
