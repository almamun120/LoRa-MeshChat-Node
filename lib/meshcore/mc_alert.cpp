#include "mc_alert.h"

namespace mc {

int AlertPlayer::priority(AlertKind k) const {
    switch (k) {
        case AlertKind::Sos: return 3;
        case AlertKind::Private: return 2;
        default: return 1;
    }
}

void AlertPlayer::apply(bool led, int hz) {
    if (!sound_) hz = 0;
    if (int(led) != lastLed_) { lastLed_ = led; if (out_.led) out_.led(led); }
    if (hz != lastHz_) { lastHz_ = hz; if (out_.tone) out_.tone(hz); }
}

void AlertPlayer::start(const std::vector<Step>& steps, int prio, uint32_t now) {
    if (!queue_.empty() && prio < curPrio_) return;        // a louder alert is playing: the quieter one is dropped
    queue_ = steps;
    pos_ = 0;
    curPrio_ = prio;
    stepEnd_ = now + steps[0].ms;
}

void AlertPlayer::trigger(AlertKind k, uint32_t now) {
    if (!enabled_) return;
    switch (k) {
        case AlertKind::LoraRx: case AlertKind::LoraTx:
            pulseUntil_ = now + 40;                         // a short flash of the light, never a sound
            return;
        case AlertKind::Boot:
            start({{880, 80, true}, {0, 40, true}, {1320, 80, true}}, 1, now);
            break;
        case AlertKind::Link:
            start({{1000, 60, true}}, 1, now);
            break;
        case AlertKind::Public:
            if (havePublic_ && now - lastPublic_ < 3000) return;     // a busy board must not turn into a siren
            havePublic_ = true;
            lastPublic_ = now;
            start({{1200, 70, true}}, 1, now);
            break;
        case AlertKind::Private:
            unread_ = true;                                  // the light stays on until somebody presses a key
            start({{1500, 90, true}, {0, 60, true}, {1900, 120, true}}, 2, now);
            break;
        case AlertKind::Sos:
            sos_ = true;
            sosAcked_ = false;
            nextSos_ = now + 5000;
            start({{2200, 150, true}, {0, 100, true}, {1800, 150, true}, {0, 100, true}, {2200, 150, true}, {0, 100, true}, {1800, 150, true}, {0, 100, false}}, 3, now);
            break;
    }
}

void AlertPlayer::sosActive(bool on, uint32_t now) {
    if (!enabled_) return;
    if (on && !sos_) trigger(AlertKind::Sos, now);
    if (!on) { sos_ = false; sosAcked_ = false; }
}

void AlertPlayer::acknowledge() {
    unread_ = false;
    if (sos_) sosAcked_ = true;
}

void AlertPlayer::tick(uint32_t now) {
    if (!enabled_) { apply(false, 0); return; }
    if (!queue_.empty()) {
        while (!queue_.empty() && int32_t(now - stepEnd_) >= 0) {
            pos_++;
            if (pos_ >= queue_.size()) { queue_.clear(); curPrio_ = 0; break; }
            stepEnd_ += queue_[pos_].ms;
        }
    }
    if (queue_.empty() && sos_ && !sosAcked_ && int32_t(now - nextSos_) >= 0) {
        nextSos_ = now + 5000;
        start({{2200, 150, true}, {0, 100, true}, {1800, 150, true}, {0, 100, true}, {2200, 150, true}, {0, 100, false}}, 3, now);
    }
    bool led = unread_ || int32_t(pulseUntil_ - now) > 0;
    int hz = 0;
    if (!queue_.empty()) { led = led || queue_[pos_].led; hz = queue_[pos_].hz; }
    if (sos_ && !sosAcked_ && queue_.empty()) led = led || ((now / 500) % 2 == 0);     // the light keeps blinking between the beeps
    apply(led, hz);
}

}  // namespace mc
