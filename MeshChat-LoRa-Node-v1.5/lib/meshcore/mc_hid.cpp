#include "mc_hid.h"
#include <cstring>

namespace mc {

static const char kPlain[]   = "abcdefghijklmnopqrstuvwxyz1234567890";
static const char kShifted[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()";

// HID usage 0x2D..0x38: - = [ ] \ (non-US #) ; ' ` , . /
static const char kSym[]   = {'-', '=', '[', ']', '\\', '#', ';', '\'', '`', ',', '.', '/'};
static const char kSymSh[] = {'_', '+', '{', '}', '|', '~', ':', '"', '~', '<', '>', '?'};

bool HidKeyboardDecoder::report(const uint8_t* d, size_t n, std::vector<KeyEvent>& out) {
    if (!d) return false;
    const uint8_t* r = d;
    if (n == 9 && d[0] <= 0x0F && d[2] == 0) r = d + 1;          // a report ID in front
    else if (n != 8 || d[1] != 0) return false;                    // not an 8-byte keyboard report
    uint8_t mods = r[0];
    const uint8_t* keys = r + 2;
    for (int i = 0; i < 6; i++) if (keys[i] == 0x01) return true;  // "too many keys down": ignore this report, keep the old state
    bool shift = mods & 0x22;                                      // left or right shift
    bool command = mods & 0xDD;                                    // ctrl, alt, gui: shortcuts, nothing to type
    for (int i = 0; i < 6; i++) {
        uint8_t k = keys[i];
        if (k == 0) continue;
        bool wasDown = false;
        for (int j = 0; j < 6; j++) if (prev_[j] == k) wasDown = true;
        if (wasDown) continue;
        KeyEvent ev;
        if (k >= 0x04 && k <= 0x1D) {                              // letters
            if (command) continue;
            bool up = shift != caps_;
            ev.key = Key::Char;
            ev.ch = up ? kShifted[k - 0x04] : kPlain[k - 0x04];
        } else if (k >= 0x1E && k <= 0x27) {                       // digits
            if (command) continue;
            ev.key = Key::Char;
            ev.ch = shift ? kShifted[26 + (k - 0x1E)] : kPlain[26 + (k - 0x1E)];
        } else if (k >= 0x2D && k <= 0x38) {    // punctuation
            if (command) continue;
            ev.key = Key::Char;
            ev.ch = shift ? kSymSh[k - 0x2D] : kSym[k - 0x2D];
        } else if (k >= 0x59 && k <= 0x62) {                       // keypad 1..9, 0
            if (command) continue;
            ev.key = Key::Char;
            ev.ch = kPlain[26 + (k == 0x62 ? 9 : k - 0x59)];
        } else switch (k) {
            case 0x28: case 0x58: ev.key = Key::Enter; break;
            case 0x29: ev.key = Key::Esc; break;
            case 0x2A: ev.key = Key::Backspace; break;
            case 0x2B: ev.key = Key::Tab; break;
            case 0x2C: if (command) continue; ev.key = Key::Char; ev.ch = ' '; break;
            case 0x39: caps_ = !caps_; continue;                   // caps lock: tracked here, the keyboard's own light may differ
            case 0x3A: ev.key = Key::F1; break;
            case 0x3B: ev.key = Key::F2; break;
            case 0x3C: ev.key = Key::F3; break;
            case 0x3D: ev.key = Key::F4; break;
            case 0x4C: ev.key = Key::Delete; break;
            case 0x4F: ev.key = Key::Right; break;
            case 0x50: ev.key = Key::Left; break;
            case 0x51: ev.key = Key::Down; break;
            case 0x52: ev.key = Key::Up; break;
            case 0x54: if (command) continue; ev.key = Key::Char; ev.ch = '/'; break;
            case 0x55: if (command) continue; ev.key = Key::Char; ev.ch = '*'; break;
            case 0x56: if (command) continue; ev.key = Key::Char; ev.ch = '-'; break;
            case 0x57: if (command) continue; ev.key = Key::Char; ev.ch = '+'; break;
            case 0x63: if (command) continue; ev.key = Key::Char; ev.ch = '.'; break;
            default: continue;
        }
        out.push_back(ev);
    }
    memcpy(prev_, keys, 6);
    return true;
}

void HidKeyboardDecoder::reset() { memset(prev_, 0, sizeof prev_); }

}  // namespace mc
