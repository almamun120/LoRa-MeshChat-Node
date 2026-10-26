// The weather station part of this node: reads the BMP280 and broadcasts "[WX] 27.5°C | 1008.2 hPa" every N minutes.
#pragma once
#include <Arduino.h>
#include "bmp280.h"
#include "mc_node.h"
#include "settings.h"

class WeatherStation {
public:
    void begin(Settings* s);
    void poll(mc::RelayNode* node, uint32_t now);
    /** Applies a new interval / on-off at once (no reboot) and reschedules. */
    void configure(bool on, int minutes);
    /** Take a reading and broadcast it as soon as possible. */
    void sendSoon() { due_ = millis(); }
    bool sensorOk() const { return sensor_.ok(); }
    int sensorAddr() const { return sensor_.addr(); }
    bool hasReading() const { return have_; }
    mc::WeatherReading last() const { return last_; }
    uint32_t sentCount() const { return sent_; }
    /** Seconds until the next broadcast (0 when off or no sensor). */
    uint32_t nextInSec(uint32_t now) const;
private:
    void schedule(uint32_t now, bool first);
    Settings* set_ = nullptr;
    Bmp280 sensor_;
    mc::WeatherReading last_;
    bool have_ = false, on_ = true;
    int minutes_ = 5;
    uint32_t due_ = 0, nextProbe_ = 0, sent_ = 0;
};
