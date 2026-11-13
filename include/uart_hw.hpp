#pragma once

#include <cstdint>

class UartHw {
public:
    UartHw() {}
    bool init(std::uint32_t peripheral_hz = 144000000u) noexcept;
    bool put(char c) noexcept;
    void print(const char* s) noexcept;
    void print_int(std::int32_t n) noexcept;
    void print_fixed(float value, unsigned decimals = 2) noexcept;
};
