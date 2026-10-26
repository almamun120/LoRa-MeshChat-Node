#include "mc_ps2.h"

namespace mc {

// scan code set 2 -> {unshifted, shifted}
static bool ascii(uint8_t sc, char& lo, char& hi) {
    static const struct { uint8_t sc; char lo, hi; } map[] = {
        {0x1C, 'a', 'A'}, {0x32, 'b', 'B'}, {0x21, 'c', 'C'}, {0x23, 'd', 'D'}, {0x24, 'e', 'E'}, {0x2B, 'f', 'F'},
        {0x34, 'g', 'G'}, {0x33, 'h', 'H'}, {0x43, 'i', 'I'}, {0x3B, 'j', 'J'}, {0x42, 'k', 'K'}, {0x4B, 'l', 'L'},
        {0x3A, 'm', 'M'}, {0x31, 'n', 'N'}, {0x44, 'o', 'O'}, {0x4D, 'p', 'P'}, {0x15, 'q', 'Q'}, {0x2D, 'r', 'R'},
        {0x1B, 's', 'S'}, {0x2C, 't', 'T'}, {0x3C, 'u', 'U'}, {0x2A, 'v', 'V'}, {0x1D, 'w', 'W'}, {0x22, 'x', 'X'},
        {0x35, 'y', 'Y'}, {0x1A, 'z', 'Z'},
        {0x45, '0', ')'}, {0x16, '1', '!'}, {0x1E, '2', '@'}, {0x26, '3', '#'}, {0x25, '4', '$'}, {0x2E, '5', '%'},
        {0x36, '6', '^'}, {0x3D, '7', '&'}, {0x3E, '8', '*'}, {0x46, '9', '('},
        {0x29, ' ', ' '}, {0x0E, '`', '~'}, {0x4E, '-', '_'}, {0x55, '=', '+'}, {0x54, '[', '{'}, {0x5B, ']', '}'},
        {0x5D, '\\', '|'}, {0x4C, ';', ':'}, {0x52, '\'', '"'}, {0x41, ',', '<'}, {0x49, '.', '>'}, {0x4A, '/', '?'},
    };
    for (auto& m : map) if (m.sc == sc) { lo = m.lo; hi = m.hi; return true; }
    return false;
}

KeyEvent Ps2Decoder::push(uint8_t b) {
    KeyEvent ev;
    if (b == 0xE0) { ext_ = true; return ev; }
    if (b == 0xF0) { brk_ = true; return ev; }
    if (b == 0xFA || b == 0xAA || b == 0xFE) return ev;        // ACK / self-test / resend from the keyboard
    bool released = brk_, ext = ext_;
    brk_ = ext_ = false;
    if (!ext && (b == 0x12 || b == 0x59)) { shift_ = !released; return ev; }
    if (released) return ev;
    if (!ext && b == 0x58) { caps_ = !caps_; return ev; }
    if (ext) {
        switch (b) {
            case 0x75: ev.key = Key::Up; break;
            case 0x72: ev.key = Key::Down; break;
            case 0x6B: ev.key = Key::Left; break;
            case 0x74: ev.key = Key::Right; break;
            case 0x71: ev.key = Key::Delete; break;
            case 0x5A: ev.key = Key::Enter; break;             // keypad enter
            default: break;
        }
        return ev;
    }
    switch (b) {
        case 0x5A: ev.key = Key::Enter; return ev;
        case 0x66: ev.key = Key::Backspace; return ev;
        case 0x0D: ev.key = Key::Tab; return ev;
        case 0x76: ev.key = Key::Esc; return ev;
        case 0x05: ev.key = Key::F1; return ev;
        case 0x06: ev.key = Key::F2; return ev;
        case 0x04: ev.key = Key::F3; return ev;
        case 0x0C: ev.key = Key::F4; return ev;
        default: break;
    }
    char lo, hi;
    if (ascii(b, lo, hi)) {
        bool letter = lo >= 'a' && lo <= 'z';
        bool up = letter ? (shift_ != caps_) : shift_;
        ev.key = Key::Char;
        ev.ch = up ? hi : lo;
    }
    return ev;
}

}  // namespace mc
