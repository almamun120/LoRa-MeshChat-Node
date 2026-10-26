// SX1278 (RA-02) through RadioLib.
#pragma once
#include <Arduino.h>
#include <functional>
#include "mc_bytes.h"
#include "settings.h"

class LoraRadio {
public:
    std::function<void(const mc::Bytes& frame, float rssi, float snr)> onFrame;
    bool begin(const Settings& s);
    void poll();
    bool transmit(const mc::Bytes& frame);     // blocks for the time on air
    bool channelBusy();                        // true while another radio is sending (channel activity detection)
    bool ok() const { return ok_; }
    int error() const { return err_; }
    float lastRssi() const { return rssi_; }
    float lastSnr() const { return snr_; }
private:
    bool ok_ = false;
    int err_ = 0;
    float rssi_ = 0, snr_ = 0;
};
