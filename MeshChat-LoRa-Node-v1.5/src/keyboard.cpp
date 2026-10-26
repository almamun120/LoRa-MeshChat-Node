#include "keyboard.h"
#include "config.h"

static volatile uint8_t ring[64];
static volatile uint8_t head = 0, tail = 0;

static void IRAM_ATTR ps2Isr() {
    static uint8_t bits = 0, data = 0;
    static uint32_t last = 0;
    uint32_t now = micros();
    if (now - last > 2000) { bits = 0; data = 0; }      // a pause means a new frame started
    last = now;
    int b = digitalRead(PIN_PS2_DATA);
    bits++;
    if (bits >= 2 && bits <= 9) data |= (b ? 1 : 0) << (bits - 2);   // start bit, 8 data bits LSB first, parity, stop
    else if (bits == 11) {
        uint8_t n = (head + 1) & 63;
        if (n != tail) { ring[head] = data; head = n; }
        bits = 0; data = 0;
    }
}

void Keyboard::begin() {
    pinMode(PIN_PS2_CLK, INPUT);
    pinMode(PIN_PS2_DATA, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_PS2_CLK), ps2Isr, FALLING);
}

bool Keyboard::next(mc::KeyEvent& ev) {
    while (tail != head) {
        uint8_t b = ring[tail];
        tail = (tail + 1) & 63;
        ev = dec_.push(b);
        if (ev.key != mc::Key::None) return true;
    }
    while (Serial.available()) {
        int c = Serial.read();
        if (c == '\r' || c == '\n') { ev.key = mc::Key::Enter; return true; }
        if (c == 0x08 || c == 0x7F) { ev.key = mc::Key::Backspace; return true; }
        if (c == '\t') { ev.key = mc::Key::Tab; return true; }
        if (c == 0x1B) { ev.key = mc::Key::Esc; return true; }
        if (c >= 32) { ev.key = mc::Key::Char; ev.ch = char(c); return true; }
    }
    return false;
}
