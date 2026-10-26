#include "ui.h"
#include <algorithm>

using namespace mc;

void ChatUi::begin(RelayNode* n, Lcd* l, Settings* s, const String& ip, bool loraOk) {
    node_ = n; lcd_ = l; set_ = s; ip_ = ip; loraOk_ = loraOk;
    lastSeq_ = n->log().empty() ? 0 : n->log().back().seq;
}

String ChatUi::targetName() const {
    if (target_ == 0 || target_ > int(targets_.size())) return "Public board";
    return String(targets_[target_ - 1].name.c_str());
}

const ChatMsg* ChatUi::newest() const {
    return node_->log().empty() ? nullptr : &node_->log().back();
}

String ChatUi::lineFor(const ChatMsg& m) const {
    String s = m.dir == ChatMsg::OUT ? "me" : String(m.fromName.c_str());
    if (m.dir == ChatMsg::SYS) s = "SOS " + s;
    else if (!m.pub) s += m.dir == ChatMsg::OUT ? "->" : ">";
    return s;
}

void ChatUi::send() {
    if (input_.empty()) return;
    std::string err;
    bool ok;
    if (target_ == 0 || target_ > int(targets_.size())) ok = node_->sendPublic(set_->name.c_str(), input_, &err);
    else ok = node_->sendPrivate(targets_[target_ - 1].id, input_, "", &err);
    note_ = ok ? String("Sent") : String(err.c_str());
    noteUntil_ = millis() + 2500;
    if (ok) input_.clear();
}

void ChatUi::key(const KeyEvent& e) {
    uint32_t now = millis();
    composeUntil_ = now + 12000;
    switch (e.key) {
        case Key::Char: if (input_.size() < 500) input_ += e.ch; break;
        case Key::Backspace:
            while (!input_.empty() && (uint8_t(input_.back()) & 0xC0) == 0x80) input_.pop_back();   // whole UTF-8 character
            if (!input_.empty()) input_.pop_back();
            break;
        case Key::Enter: send(); break;
        case Key::Esc: input_.clear(); composeUntil_ = 0; break;
        case Key::Tab:
            targets_ = node_->contacts();
            target_ = (target_ + 1) % (int(targets_.size()) + 1);
            break;
        case Key::F1: view_ = (view_ + 1) % 4; wxIdx_ = 0; viewAt_ = now; composeUntil_ = 0; break;
        case Key::Up: case Key::Down: {
            int n = int(node_->log().size());
            if (!n) break;
            if (browse_ < 0) browse_ = n - 1;
            browse_ += e.key == Key::Up ? -1 : 1;
            browse_ = std::max(0, std::min(n - 1, browse_));
            flashUntil_ = now + 10000;
            composeUntil_ = 0;
            scroll_ = 0;
            break;
        }
        default: break;
    }
}

void ChatUi::tick(uint32_t now) {
    if (!lcd_) return;
    const ChatMsg* last = newest();
    if (last && last->seq != lastSeq_) {
        lastSeq_ = last->seq;
        if (last->dir != ChatMsg::OUT) { flashUntil_ = now + 8000; browse_ = -1; scroll_ = 0; }
    }
    if (now < noticeUntil_) { lcd_->show(notice1_, notice2_); return; }
    if (now < noteUntil_) { lcd_->show("To: " + targetName(), note_); return; }
    if (!input_.empty() || now < composeUntil_) {
        String in(input_.c_str());
        // show the tail of what is being typed
        lcd_->show("To:" + targetName(), in.length() > 16 ? in.substring(in.length() - 16) : in + "_");
        return;
    }
    for (auto& a : node_->alerts()) {
        if (a.active) {
            lcd_->show((now / 500) % 2 ? "!!!  S O S  !!!" : "                ", String(a.name.c_str()));
            return;
        }
    }
    const ChatMsg* show = nullptr;
    if (now < flashUntil_) {
        if (browse_ >= 0 && browse_ < int(node_->log().size())) show = &node_->log()[browse_];
        else show = last;
    } else browse_ = -1;
    if (!show && view_ == 2) show = last;
    if (show) {
        String body(show->text.c_str());
        String padded = body + "   ";
        if (now - scrollAt_ > 350) { scrollAt_ = now; scroll_++; }
        String l2;
        if (body.length() <= 16) l2 = body;
        else { int off = scroll_ % padded.length(); String w = padded + padded; l2 = w.substring(off, off + 16); }
        lcd_->show(lineFor(*show), l2);
        return;
    }
    if (now - viewAt_ > 3500) {
        viewAt_ = now;
        if (view_ == 3) {                                         // weather: one station per step, then back to the status pages
            if (++wxIdx_ >= int(node_->stations().size())) { wxIdx_ = 0; view_ = 0; }
        } else {
            view_ = (view_ + 1) % 4;
            wxIdx_ = 0;
            if (view_ == 3 && node_->stations().empty()) view_ = 0;   // nothing to show unless a station was heard
        }
    }
    if (view_ == 3) {
        auto st = node_->stations();
        if (st.empty()) { lcd_->show("Weather", "no station yet"); return; }
        if (wxIdx_ >= int(st.size())) wxIdx_ = 0;
        const Station& s = st[wxIdx_];
        uint32_t age = (now - s.heardAt) / 1000;
        char a[8];
        if (age < 90) snprintf(a, sizeof a, "now"); else if (age < 5400) snprintf(a, sizeof a, "%um", unsigned(age / 60)); else snprintf(a, sizeof a, "%uh", unsigned(age / 3600));
        String nm(s.name.c_str());
        if (nm.length() > 12) nm = nm.substring(0, 12);
        while (nm.length() < 12) nm += ' ';
        char l2[24];
        snprintf(l2, sizeof l2, "%.1fC %.1fhPa", double(s.r.tempC), double(s.r.pressHpa));
        char l1[24];
        snprintf(l1, sizeof l1, "%s%4s", nm.c_str(), a);
        lcd_->show(String(l1), String(l2));
        return;
    }
    if (view_ == 0) {
        char l2[24];
        snprintf(l2, sizeof l2, "BLE:%u LoRa:%u/%u", unsigned(node_->bleLinks().size()), unsigned(node_->stats().rxLora), unsigned(node_->stats().txLora));
        lcd_->show(String(mc::idLabel(node_->id()).c_str()), loraOk_ ? String(l2) : String("LoRa ERROR"));
    } else if (view_ == 1) {
        lcd_->show("WiFi:" + set_->apSsid, "http://" + ip_);
    } else {
        lcd_->show("No messages yet", "Tab=who Enter=send");
    }
}

void ChatUi::notice(const String& l1, const String& l2, uint32_t ms) { notice1_ = l1; notice2_ = l2; noticeUntil_ = millis() + ms; }
