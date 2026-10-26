#include "lcd.h"
#include "config.h"
#if LCD_I2C
  #include <Wire.h>
  #include <LiquidCrystal_I2C.h>
  static LiquidCrystal_I2C* lcd = nullptr;
#else
  #include <LiquidCrystal.h>
  static LiquidCrystal* lcd = nullptr;
#endif

static String clean(const String& s) {
    String o;
    for (size_t i = 0; i < s.length() && o.length() < 16; i++) {
        uint8_t c = s[i];
        if (c >= 32 && c < 127) o += char(c);
        else if (c >= 0xC0) o += '?';            // first byte of a multi-byte character: one '?' for the whole character
        else if (c < 0x80) o += ' ';
    }
    while (o.length() < 16) o += ' ';
    return o;
}

void Lcd::begin(int addr) {
#if LCD_I2C
    Wire.begin(PIN_LCD_SDA, PIN_LCD_SCL);
    lcd = new LiquidCrystal_I2C(addr, 16, 2);
    lcd->init();
    lcd->backlight();
#else
    (void)addr;
    lcd = new LiquidCrystal(PIN_LCD_RS, PIN_LCD_EN, PIN_LCD_D4, PIN_LCD_D5, PIN_LCD_D6, PIN_LCD_D7);
    lcd->begin(16, 2);
#endif
    lcd->clear();
}

void Lcd::show(const String& a, const String& b) {
    if (!lcd) return;
    String x = clean(a), y = clean(b);
    if (x != l1_) { lcd->setCursor(0, 0); lcd->print(x); l1_ = x; }
    if (y != l2_) { lcd->setCursor(0, 1); lcd->print(y); l2_ = y; }
}
