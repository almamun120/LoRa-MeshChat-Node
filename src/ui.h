// Keyboard + LCD chat: type a message, Tab picks who it goes to (Public board or one person), Enter sends.
#pragma once
#include <Arduino.h>
#include <vector>
#include "mc_node.h"
#include "mc_ps2.h"
#include "lcd.h"
#include "settings.h"

class ChatUi {
public:
    void begin(mc::RelayNode* node, Lcd* lcd, Settings* s, const String& ip, bool loraOk);
    void key(const mc::KeyEvent& e);
    void tick(uint32_t now);
private:
    mc::RelayNode* node_ = nullptr;
    Lcd* lcd_ = nullptr;
    Settings* set_ = nullptr;
    String ip_;
    bool loraOk_ = false;
    std::string input_;
    int target_ = 0;                    // 0 = public board, n = n-th contact
    std::vector<mc::Contact> targets_;
    uint32_t lastSeq_ = 0, flashUntil_ = 0, noteUntil_ = 0, composeUntil_ = 0, viewAt_ = 0, scrollAt_ = 0;
    int view_ = 0, scroll_ = 0, browse_ = -1, wxIdx_ = 0;
    String note_;
    String targetName() const;
    const mc::ChatMsg* newest() const;
    String lineFor(const mc::ChatMsg& m) const;
    void send();
};
