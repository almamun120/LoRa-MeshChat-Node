// Indicator light + small speaker: decides WHEN the LED is on and WHICH tone plays. No Arduino code in here, so it is tested on a PC.
// The board code (src/alerts.cpp) turns the two callbacks into a GPIO level and a PWM tone.
#pragma once
#include <cstdint>
#include <functional>
#include <vector>

namespace mc {

enum class AlertKind : uint8_t { Boot, Link, Public, Private, Sos, LoraRx, LoraTx };

class AlertPlayer {
public:
    struct Out {
        std::function<void(bool on)> led;        // indicator LED
        std::function<void(int hz)> tone;        // 0 = silence
    };
    explicit AlertPlayer(Out o) : out_(std::move(o)) {}

    /** enabled = false: no light and no sound at all. sound = false: light only (quiet mode, e.g. at night). */
    void configure(bool enabled, bool sound) { enabled_ = enabled; sound_ = sound; if (!enabled_) { queue_.clear(); unread_ = false; sos_ = false; } }
    /** An event happened. */
    void trigger(AlertKind k, uint32_t now);
    /** There is (true) / is no (false) active SOS around: while there is, the SOS pattern repeats until [acknowledge]. */
    void sosActive(bool on, uint32_t now);
    /** Somebody pressed a key: the "unread" light goes out and a repeating SOS goes quiet until the next new SOS. */
    void acknowledge();
    void tick(uint32_t now);

    bool unread() const { return unread_; }

private:
    struct Step { uint16_t hz; uint16_t ms; bool led; };
    int priority(AlertKind k) const;
    void start(const std::vector<Step>& steps, int prio, uint32_t now);
    void apply(bool led, int hz);

    Out out_;
    bool enabled_ = true, sound_ = true;
    std::vector<Step> queue_;
    size_t pos_ = 0;
    uint32_t stepEnd_ = 0;
    int curPrio_ = 0;
    bool unread_ = false, sos_ = false, sosAcked_ = false;
    uint32_t nextSos_ = 0, lastPublic_ = 0, pulseUntil_ = 0;
    bool havePublic_ = false;
    int lastLed_ = -1, lastHz_ = -1;
};

}  // namespace mc
