// BMP280 temperature / pressure sensor on I2C (Bosch, chip id 0x58; the BME280, id 0x60, works too for temperature and pressure). The maths is in lib/meshcore/mc_weather.* (tested on a PC).
#pragma once
#include <Arduino.h>
#include "mc_weather.h"

class Bmp280 {
public:
    /** addr 0 = try 0x76 then 0x77. Returns false when no BMP280 answers. */
    bool begin(int addr = 0);
    /** One measurement (forced mode: the chip sleeps in between, so there is no self-heating to speak of). */
    bool read(mc::WeatherReading& out);
    bool ok() const { return ok_; }
    int addr() const { return addr_; }
private:
    bool probe(int addr);
    bool readRegs(uint8_t reg, uint8_t* buf, size_t n);
    bool writeReg(uint8_t reg, uint8_t val);
    mc::Bmp280Calib cal_;
    int addr_ = 0;
    bool ok_ = false;
};
