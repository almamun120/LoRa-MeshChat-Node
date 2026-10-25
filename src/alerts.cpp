#include "alerts.h"

static const int kBuzzChannel = 0;

void Alerts::begin(const Settings& s) {
    ledPin_ = s.ledPin;
    buzPin_ = s.buzPin;
    if (ledPin_ >= 0) { pinMode(ledPin_, OUTPUT); digitalWrite(ledPin_, LOW); }
    if (buzPin_ >= 0) { ledcSetup(kBuzzChannel, 2000, 8); ledcAttachPin(buzPin_, kBuzzChannel); ledcWriteTone(kBuzzChannel, 0); }
    mc::AlertPlayer::Out out;
    out.led = [this](bool on) { if (ledPin_ >= 0) digitalWrite(ledPin_, on ? HIGH : LOW); };
    out.tone = [this](int hz) { if (buzPin_ >= 0) ledcWriteTone(kBuzzChannel, hz > 0 ? hz : 0); };
    player_ = new mc::AlertPlayer(out);
    player_->configure(s.alertsOn, s.alertSound);
    player_->trigger(mc::AlertKind::Boot, millis());
}

void Alerts::poll(mc::RelayNode* node, int bleLinks, uint32_t now) {
    if (!player_) return;
    const auto& log = node->log();
    uint32_t newest = lastSeq_;
    for (auto it = log.rbegin(); it != log.rend() && it->seq > lastSeq_; ++it) {
        if (it->seq > newest) newest = it->seq;
        if (it->dir == mc::ChatMsg::IN) player_->trigger(it->pub ? mc::AlertKind::Public : mc::AlertKind::Private, now);
    }
    lastSeq_ = newest;
    bool sos = false;
    for (auto& a : node->alerts()) if (a.active) sos = true;
    player_->sosActive(sos, now);
    const mc::Stats& st = node->stats();
    if (st.rxLora != rx_) { rx_ = st.rxLora; player_->trigger(mc::AlertKind::LoraRx, now); }
    if (st.txLora != tx_) { tx_ = st.txLora; player_->trigger(mc::AlertKind::LoraTx, now); }
    if (bleLinks > links_) player_->trigger(mc::AlertKind::Link, now);
    links_ = bleLinks;
    player_->tick(now);
}
