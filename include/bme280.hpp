#pragma once
#include "i2c.hpp"
#include <cstdint>
class Bme280 {
public:
    explicit Bme280(I2c& bus, std::uint8_t address = 0x76) noexcept : bus_(bus), address_(address) {}
    void set_address(std::uint8_t address) noexcept { address_ = address; }
    std::uint8_t address() const noexcept { return address_; }
    bool init() noexcept;
    bool read() noexcept;
    float temperature() const noexcept { return temperature_; } // Celsius
    float humidity() const noexcept { return humidity_; } // % rF
    float pressure() const noexcept { return pressure_; } // hPa
private:
    bool get(std::uint8_t reg, std::uint8_t* data, unsigned count) noexcept;
    bool put(std::uint8_t reg, std::uint8_t value) noexcept;
    I2c& bus_;
    std::uint8_t address_;
    std::uint16_t t1_{}, p1_{};
    std::int16_t t2_{}, t3_{}, p2_{}, p3_{}, p4_{}, p5_{}, p6_{}, p7_{}, p8_{}, p9_{};
    std::uint8_t h1_{}, h3_{};
    std::int16_t h2_{}, h4_{}, h5_{};
    std::int8_t h6_{};
    std::int32_t t_fine_{};
    float temperature_{}, humidity_{}, pressure_{};
};
