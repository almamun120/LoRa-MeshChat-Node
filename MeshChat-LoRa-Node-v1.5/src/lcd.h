#pragma once
#include <Arduino.h>

/** 16x2 character LCD (HD44780). It only knows ASCII, so other characters (Bangla etc.) are shown as '?'. */
class Lcd {
public:
    void begin(int i2cAddr);
    void show(const String& line1, const String& line2);   // padded / cut to 16 characters, only redraws when changed
private:
    String l1_, l2_;
};
