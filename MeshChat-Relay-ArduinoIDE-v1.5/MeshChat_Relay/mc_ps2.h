// PS/2 keyboard decoder (scan code set 2) -> characters and a few control keys. The ESP32 side only has to
// clock the 11-bit frames in and call push(byte); everything else is plain logic that is tested on a PC.
#pragma once
#include <cstdint>

namespace mc {

enum class Key : uint8_t { None, Char, Enter, Backspace, Tab, Esc, Up, Down, Left, Right, F1, F2, F3, F4, Delete };

struct KeyEvent { Key key = Key::None; char ch = 0; };

class Ps2Decoder {
public:
    /** Feed one scan code byte; returns an event for key presses (releases only update modifier state). */
    KeyEvent push(uint8_t b);
private:
    bool brk_ = false, ext_ = false, shift_ = false, caps_ = false;
};

}  // namespace mc
