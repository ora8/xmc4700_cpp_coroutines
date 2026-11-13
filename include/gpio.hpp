#pragma once
#include <cstdint>

class GpioOutput {
public:
    constexpr GpioOutput(std::uintptr_t port_base, unsigned pin) noexcept
        : port_(port_base), pin_(pin) {}

    void configure() noexcept;
    void write(bool high) noexcept;
    void toggle() noexcept;

    std::uintptr_t debug_port() const noexcept {
        return port_;
    }

    unsigned debug_pin() const noexcept {
        return pin_;
    }

private:
    std::uintptr_t port_;
    unsigned pin_;
};