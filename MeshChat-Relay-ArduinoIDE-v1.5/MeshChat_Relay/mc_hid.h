// Bluetooth (BLE) keyboard: turns the HID input reports a keyboard sends into the same KeyEvents the PS/2 decoder gives.
// Plain logic, no Arduino code, so it is tested on a PC. The board side (src/bt_keyboard.cpp) only connects and passes the bytes in.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "mc_ps2.h"

namespace mc {

class HidKeyboardDecoder {
public:
    /**
     * Feeds one HID input report. Accepted layouts: the 8-byte boot keyboard report (modifiers, reserved, 6 key codes), or the
     * same with a leading report ID byte (9 bytes). Appends one event for every key that was pressed since the previous report
     * (releases only update state; the keyboard does not repeat, a held key is typed once).
     * Returns false when the bytes are not a keyboard report (mouse, volume keys, ...).
     */
    bool report(const uint8_t* data, size_t len, std::vector<KeyEvent>& out);
    /** The keyboard disconnected: forget which keys were down. */
    void reset();

private:
    uint8_t prev_[6] = {0, 0, 0, 0, 0, 0};
    bool caps_ = false;
};

}  // namespace mc
