#pragma once
#include <cstdint>

#ifndef CPU_HZ
#define CPU_HZ 144000000UL
#endif

namespace board {
// XMC4700 PORT5 base address. Verify pin assignment against the board revision.
inline constexpr std::uintptr_t port5_base = 0x48028500UL;
inline constexpr unsigned led_port = 5;
inline constexpr unsigned led_pin = 8;
#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW 0
#endif
inline constexpr bool led_active_low = LED_ACTIVE_LOW != 0;
}
