#include "bmp280.h"
#include "config.h"
#include <Wire.h>

static const uint8_t REG_ID = 0xD0, REG_RESET = 0xE0, REG_STATUS = 0xF3, REG_CTRL = 0xF4, REG_CONFIG = 0xF5, REG_DATA = 0xF7, REG_CALIB = 0x88;

bool Bmp280::writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(uint8_t(addr_));
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

bool Bmp280::readRegs(uint8_t reg, uint8_t* buf, size_t n) {
    Wire.beginTransmission(uint8_t(addr_));
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(uint8_t(addr_), uint8_t(n)) != n) return false;
    for (size_t i = 0; i < n; i++) buf[i] = uint8_t(Wire.read());
    return true;
}

bool Bmp280::probe(int addr) {
    addr_ = addr;
    uint8_t id = 0;
    if (!readRegs(REG_ID, &id, 1) || (id != 0x58 && id != 0x57 && id != 0x56 && id != 0x60)) return false;   // 0x56/0x57 early samples, 0x58 production; 0x60 = BME280 (same temperature/pressure registers; humidity is left off)
    uint8_t raw[24];
    if (!readRegs(REG_CALIB, raw, 24)) return false;
    cal_ = mc::bmp280ParseCalib(raw);
    if (cal_.T1 == 0 || cal_.P1 == 0) return false;
    writeReg(REG_CONFIG, 0x00);          // no standby / IIR filter: we only ever take single forced measurements
    return true;
}

bool Bmp280::begin(int addr) {
#if !LCD_I2C
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);          // with the I2C LCD the bus was already started by lcd.begin()
#endif
    ok_ = false;
    if (addr > 0) ok_ = probe(addr);
    else ok_ = probe(0x76) || probe(0x77);
    return ok_;
}

bool Bmp280::read(mc::WeatherReading& out) {
    if (!ok_) return false;
    // temperature x2, pressure x16, forced mode: one measurement, then the chip goes back to sleep
    if (!writeReg(REG_CTRL, (2 << 5) | (5 << 2) | 1)) { ok_ = false; return false; }
    uint32_t t0 = millis();
    uint8_t st = 0xFF;
    do {
        delay(8);
        if (!readRegs(REG_STATUS, &st, 1)) { ok_ = false; return false; }
    } while ((st & 0x08) && millis() - t0 < 120);       // bit 3 = measuring
    if (st & 0x08) return false;
    uint8_t d[6];
    if (!readRegs(REG_DATA, d, 6)) { ok_ = false; return false; }
    int32_t adcP = (int32_t(d[0]) << 12) | (int32_t(d[1]) << 4) | (d[2] >> 4);
    int32_t adcT = (int32_t(d[3]) << 12) | (int32_t(d[4]) << 4) | (d[5] >> 4);
    return mc::bmp280Compensate(cal_, adcT, adcP, out);
}
