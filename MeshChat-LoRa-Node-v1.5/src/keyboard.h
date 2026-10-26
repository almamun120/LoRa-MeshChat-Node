// Key input: a PS/2 keyboard on two GPIOs, plus the USB serial console (type in a terminal on a PC - works with any keyboard).
// The ESP32 in a DevKit V1 has no USB host, so a plain USB keyboard cannot be plugged in directly (see docs/WIRING.md).
#pragma once
#include <Arduino.h>
#include "mc_ps2.h"

class Keyboard {
public:
    void begin();
    /** Returns true and fills [ev] while there are key events waiting. UTF-8 bytes from the serial console come as Char events. */
    bool next(mc::KeyEvent& ev);
private:
    mc::Ps2Decoder dec_;
};
