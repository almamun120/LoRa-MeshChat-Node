// Weather station support. No Arduino code in here, so it is tested on a PC.
//  - Bmp280Calib/bmp280Compensate: the datasheet integer compensation (raw ADC -> degC and hPa)
//  - formatWeather/parseWeather:   the text a station broadcasts on the public board, e.g.  "[WX] 27.5°C | 1008.2 hPa"
//  - WeatherBoard:                 the stations heard so far (this node included)
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mc {

struct WeatherReading { float tempC = 0; float pressHpa = 0; };

// ---------------------------------------------------------------- BMP280
struct Bmp280Calib {
    uint16_t T1 = 0; int16_t T2 = 0, T3 = 0;
    uint16_t P1 = 0; int16_t P2 = 0, P3 = 0, P4 = 0, P5 = 0, P6 = 0, P7 = 0, P8 = 0, P9 = 0;
};
/** Parses the 24 calibration bytes at register 0x88..0x9F (little endian). */
Bmp280Calib bmp280ParseCalib(const uint8_t raw[24]);
/** adcT / adcP: the 20-bit raw values of registers 0xFA.. / 0xF7... Returns false for "no measurement" (0x80000) or a nonsense result. */
bool bmp280Compensate(const Bmp280Calib& c, int32_t adcT, int32_t adcP, WeatherReading& out);

// ---------------------------------------------------------------- the identity field
/** Rounds to the wire units (0.1 degC, 0.1 hPa). false if the numbers are not believable (-60..80 degC, 300..1100 hPa). */
bool weatherToWire(const WeatherReading& r, int16_t& t10, uint16_t& p10);
inline WeatherReading weatherFromWire(int16_t t10, uint16_t p10) { WeatherReading r; r.tempC = t10 / 10.0f; r.pressHpa = p10 / 10.0f; return r; }

/** One line of the device list: this node (local) or a contact that sent weather with its identity. */
struct Station {
    uint32_t id = 0;
    std::string name;
    WeatherReading r;
    uint32_t heardAt = 0;      // this node's millis() when the report arrived
    bool local = false;        // our own sensor
    uint8_t via = 0;           // 0 local, 1 BLE, 2 LoRa
};

}  // namespace mc
