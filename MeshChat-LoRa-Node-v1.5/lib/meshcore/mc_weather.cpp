#include "mc_weather.h"
#include <cmath>

namespace mc {

// ---------------------------------------------------------------- BMP280 (datasheet BST-BMP280-DS001, section 3.11.3, 32-bit integer version)

static uint16_t u16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
static int16_t s16(const uint8_t* p) { return int16_t(u16(p)); }

Bmp280Calib bmp280ParseCalib(const uint8_t r[24]) {
    Bmp280Calib c;
    c.T1 = u16(r + 0);  c.T2 = s16(r + 2);  c.T3 = s16(r + 4);
    c.P1 = u16(r + 6);  c.P2 = s16(r + 8);  c.P3 = s16(r + 10); c.P4 = s16(r + 12); c.P5 = s16(r + 14);
    c.P6 = s16(r + 16); c.P7 = s16(r + 18); c.P8 = s16(r + 20); c.P9 = s16(r + 22);
    return c;
}

bool bmp280Compensate(const Bmp280Calib& c, int32_t adcT, int32_t adcP, WeatherReading& out) {
    if (adcT == 0x80000 || adcP == 0x80000 || c.T1 == 0 || c.P1 == 0) return false;   // 0x80000 = measurement skipped
    int32_t v1 = ((((adcT >> 3) - (int32_t(c.T1) << 1))) * int32_t(c.T2)) >> 11;
    int32_t v2 = (((((adcT >> 4) - int32_t(c.T1)) * ((adcT >> 4) - int32_t(c.T1))) >> 12) * int32_t(c.T3)) >> 14;
    int32_t tFine = v1 + v2;
    float tempC = float((tFine * 5 + 128) >> 8) / 100.0f;

    int64_t a = int64_t(tFine) - 128000;
    int64_t b = a * a * int64_t(c.P6);
    b += (a * int64_t(c.P5)) << 17;
    b += int64_t(c.P4) << 35;
    a = ((a * a * int64_t(c.P3)) >> 8) + ((a * int64_t(c.P2)) << 12);
    a = (((int64_t(1) << 47) + a) * int64_t(c.P1)) >> 33;
    if (a == 0) return false;                                       // would divide by zero
    int64_t p = 1048576 - adcP;
    p = (((p << 31) - b) * 3125) / a;
    a = (int64_t(c.P9) * (p >> 13) * (p >> 13)) >> 25;
    b = (int64_t(c.P8) * p) >> 19;
    p = ((p + a + b) >> 8) + (int64_t(c.P7) << 4);                  // Pa in Q24.8
    float hpa = float(p) / 256.0f / 100.0f;
    if (!(tempC > -60 && tempC < 90) || !(hpa > 250 && hpa < 1200)) return false;
    out.tempC = tempC;
    out.pressHpa = hpa;
    return true;
}

// ---------------------------------------------------------------- wire units

bool weatherToWire(const WeatherReading& r, int16_t& t10, uint16_t& p10) {
    if (!(r.tempC > -60.0f && r.tempC < 80.0f) || !(r.pressHpa > 300.0f && r.pressHpa < 1100.0f)) return false;
    t10 = int16_t(std::lround(r.tempC * 10.0f));
    p10 = uint16_t(std::lround(r.pressHpa * 10.0f));
    return true;
}

}  // namespace mc
