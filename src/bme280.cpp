#include "bme280.hpp"
namespace {
std::uint16_t u16(const std::uint8_t* b) { return std::uint16_t(b[0] | (std::uint16_t(b[1]) << 8)); }
std::int16_t s16(const std::uint8_t* b) { return static_cast<std::int16_t>(u16(b)); }
}
bool Bme280::get(std::uint8_t r, std::uint8_t* b, unsigned n) noexcept {
    return bus_.write_read(address_, &r, 1, b, n);
}
bool Bme280::put(std::uint8_t r, std::uint8_t v) noexcept {
    const std::uint8_t b[]{r,v}; return bus_.write(address_, b, 2);
}
bool Bme280::init() noexcept {
    std::uint8_t id{};
    if (!get(0xd0, &id, 1) || id != 0x60) return false;
    std::uint8_t a[26]{}, h[7]{};
    if (!get(0x88, a, sizeof a) || !get(0xe1, h, sizeof h)) return false;
    t1_=u16(a); t2_=s16(a+2); t3_=s16(a+4);
    p1_=u16(a+6); p2_=s16(a+8); p3_=s16(a+10); p4_=s16(a+12);
    p5_=s16(a+14); p6_=s16(a+16); p7_=s16(a+18); p8_=s16(a+20); p9_=s16(a+22);
    h1_=a[25]; h2_=s16(h); h3_=h[2];
    h4_=static_cast<std::int16_t>((std::int16_t(std::int8_t(h[3])) * 16) | (h[4] & 0x0f));
    h5_=static_cast<std::int16_t>((std::int16_t(std::int8_t(h[5])) * 16) | (h[4] >> 4));
    h6_=static_cast<std::int8_t>(h[6]);
    // Feuchte vor ctrl_meas konfigurieren; Oversampling x1, normal mode, standby 1000 ms.
    return put(0xf2, 0x01) && put(0xf5, 0xa0) && put(0xf4, 0x27);
}
bool Bme280::read() noexcept {
    std::uint8_t b[8]{};
    if (!get(0xf7, b, sizeof b)) return false;
    const std::int32_t adc_p = (std::int32_t(b[0]) << 12) | (std::int32_t(b[1]) << 4) | (b[2] >> 4);
    const std::int32_t adc_t = (std::int32_t(b[3]) << 12) | (std::int32_t(b[4]) << 4) | (b[5] >> 4);
    const std::int32_t adc_h = (std::int32_t(b[6]) << 8) | b[7];
    if (adc_t == 0x80000 || adc_p == 0x80000 || adc_h == 0x8000) return false;
    // Bosch BME280 floating point compensation equations.
    const double v1 = (double(adc_t)/16384.0 - double(t1_)/1024.0)*double(t2_);
    const double v2 = ((double(adc_t)/131072.0 - double(t1_)/8192.0) *
                       (double(adc_t)/131072.0 - double(t1_)/8192.0))*double(t3_);
    t_fine_ = static_cast<std::int32_t>(v1+v2);
    temperature_ = static_cast<float>((v1+v2)/5120.0);
    double pvar1 = double(t_fine_)/2.0 - 64000.0;
    double pvar2 = pvar1*pvar1*double(p6_)/32768.0;
    pvar2 += pvar1*double(p5_)*2.0;
    pvar2 = pvar2/4.0 + double(p4_)*65536.0;
    pvar1 = (double(p3_)*pvar1*pvar1/524288.0 + double(p2_)*pvar1)/524288.0;
    pvar1 = (1.0+pvar1/32768.0)*double(p1_);
    if (pvar1 == 0.0) return false;
    double p = 1048576.0-double(adc_p);
    p = (p-pvar2/4096.0)*6250.0/pvar1;
    pvar1 = double(p9_)*p*p/2147483648.0;
    pvar2 = p*double(p8_)/32768.0;
    p += (pvar1+pvar2+double(p7_))/16.0;
    pressure_ = static_cast<float>(p/100.0);
    double hum = double(t_fine_)-76800.0;
    hum = (double(adc_h)-(double(h4_)*64.0+double(h5_)/16384.0*hum)) *
          (double(h2_)/65536.0*(1.0+double(h6_)/67108864.0*hum *
          (1.0+double(h3_)/67108864.0*hum)));
    hum *= (1.0-double(h1_)*hum/524288.0);
    if (hum > 100.0) hum=100.0;
    if (hum < 0.0) hum=0.0;
    humidity_ = static_cast<float>(hum);
    return true;
}
