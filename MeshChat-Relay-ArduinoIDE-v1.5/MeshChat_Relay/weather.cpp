#include "weather.h"

void WeatherStation::begin(Settings* s) {
    set_ = s;
    on_ = s->wxOn;
    minutes_ = constrain(s->wxMin, 1, 1440);
    if (on_) {
        bool ok = sensor_.begin(s->wxAddr);
        Serial.printf("weather: BMP280 %s", ok ? "found" : "NOT found (check wiring; will retry)");
        if (ok) Serial.printf(" at 0x%02X", sensor_.addr());
        Serial.printf(", broadcast every %d min\n", minutes_);
    }
    schedule(millis(), true);
}

void WeatherStation::configure(bool on, int minutes) {
    on_ = on;
    minutes_ = constrain(minutes, 1, 1440);
    schedule(millis(), false);
}

void WeatherStation::schedule(uint32_t now, bool first) {
    uint32_t period = uint32_t(minutes_) * 60000UL;
    uint32_t jitter = esp_random() % (period / 10 + 1);               // +0..10 %, so stations that booted together drift apart
    // the first report comes soon after boot (radio and neighbours are up), later ones after a full interval
    due_ = now + (first ? 40000UL + esp_random() % 20000UL : period + jitter);
}

uint32_t WeatherStation::nextInSec(uint32_t now) const {
    if (!on_ || !sensor_.ok()) return 0;
    int32_t ms = int32_t(due_ - now);
    return ms > 0 ? uint32_t(ms) / 1000 : 0;
}

void WeatherStation::poll(mc::RelayNode* node, uint32_t now) {
    if (!on_) return;
    if (!sensor_.ok()) {                                              // sensor missing or unplugged: look again once a minute
        if ((int32_t)(now - nextProbe_) < 0) return;
        nextProbe_ = now + 60000UL;
        if (!sensor_.begin(set_ ? set_->wxAddr : 0)) return;
        Serial.printf("weather: BMP280 found at 0x%02X\n", sensor_.addr());
    }
    if ((int32_t)(now - due_) < 0) return;
    mc::WeatherReading r;
    if (!sensor_.read(r)) {
        due_ = now + 15000UL;                                         // a failed read: try again soon, do not wait a whole interval
        return;
    }
    last_ = r;
    have_ = true;
    node->sendWeather(r);
    sent_++;
    Serial.printf("weather: %.1f C  %.1f hPa -> broadcast #%u\n", double(r.tempC), double(r.pressHpa), (unsigned)sent_);
    schedule(now, false);
}
