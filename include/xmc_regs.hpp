#pragma once
// Gemeinsame Register-Helfer für den XMC4700 (ohne XMCLib).
//
// - xmc::reg():        32-Bit-Registerzugriff über eine feste Adresse
// - xmc::port:         Port-Basisadressen, Register-Offsets, Pin-Modi (IOCR-Feld PC)
// - xmc::core:         Cortex-M4-Register (SysTick, SCB)
//
// Modulspezifische Register (USIC, SCU-Takt) stehen weiterhin in den jeweiligen
// .cpp-Dateien und benutzen xmc::reg().

#include <cstdint>

namespace xmc {

/// 32-Bit-Register an einer festen Adresse.
inline volatile std::uint32_t &reg(std::uintptr_t address) noexcept {
  return *reinterpret_cast<volatile std::uint32_t *>(address);
}

/// Register lesen (garantierter Lesezugriff). `(void)reg(addr)` liest in C++ ein
/// volatile-Objekt NICHT; Register, deren Lesen etwas auslöst (z. B. RBUF), so lesen.
inline std::uint32_t read(std::uintptr_t address) noexcept {
  return *reinterpret_cast<volatile std::uint32_t *>(address);
}

namespace port {

/// Basisadresse von Port n (P0..P9, P14, P15): 0x48028000 + 0x100 * n
constexpr std::uintptr_t base(unsigned n) noexcept {
  return 0x48028000u + 0x100u * n;
}

inline constexpr std::uintptr_t P0 = base(0);
inline constexpr std::uintptr_t P1 = base(1);
inline constexpr std::uintptr_t P3 = base(3);
inline constexpr std::uintptr_t P5 = base(5);

// Register-Offsets ab Port-Basis
inline constexpr std::uintptr_t OUT = 0x00;   // Output Register (Soll-Pegel)
inline constexpr std::uintptr_t OMR = 0x04;   // Output Modification Register
inline constexpr std::uintptr_t IOCR0 = 0x10; // IOCR0..IOCR12, 4 Pins je Register
inline constexpr std::uintptr_t IN = 0x24;    // Input Register (Pegel am Pin)

// Werte für das PC-Feld (Bits [7:3] je Pin im IOCR-Register)
inline constexpr std::uint32_t INPUT_TRISTATE = 0x00;
inline constexpr std::uint32_t INPUT_PULL_UP = 0x02;
inline constexpr std::uint32_t OUTPUT_PUSH_PULL = 0x10;
/// Push-Pull-Ausgang der alternativen Funktion n (1..4), z. B. ALT2 = 0x12
constexpr std::uint32_t output_alt(unsigned n) noexcept {
  return OUTPUT_PUSH_PULL | n;
}

/// PC-Feld eines Pins setzen (Read-Modify-Write auf dem passenden IOCR-Register).
inline void set_pin_mode(std::uintptr_t port_base, unsigned pin,
                         std::uint32_t mode) noexcept {
  if (pin > 15u) {
    return;
  }
  auto &iocr = reg(port_base + IOCR0 + (pin / 4u) * 4u);
  const unsigned shift = (pin % 4u) * 8u + 3u;
  iocr = (iocr & ~(0x1Fu << shift)) | ((mode & 0x1Fu) << shift);
}

/// Pin auf High (true) oder Low (false) setzen, ein Zugriff auf OMR.
inline void write_pin(std::uintptr_t port_base, unsigned pin, bool high) noexcept {
  const std::uint32_t bit = 1u << pin;
  reg(port_base + OMR) = high ? bit : (bit << 16);
}

/// Pegel am Pin lesen.
inline bool read_pin(std::uintptr_t port_base, unsigned pin) noexcept {
  return (reg(port_base + IN) & (1u << pin)) != 0;
}

} // namespace port

namespace core {

// SysTick
inline constexpr std::uintptr_t SYST_CSR = 0xE000E010u;
inline constexpr std::uintptr_t SYST_RVR = 0xE000E014u;
inline constexpr std::uintptr_t SYST_CVR = 0xE000E018u;

// System Control Block
inline constexpr std::uintptr_t SCB_VTOR = 0xE000ED08u;
inline constexpr std::uintptr_t SCB_CCR = 0xE000ED14u;   // UNALIGN_TRP = Bit 3
inline constexpr std::uintptr_t SCB_CPACR = 0xE000ED88u; // CP10/CP11 = Bits 20..23

inline void dsb_isb() noexcept {
  __asm volatile("dsb");
  __asm volatile("isb");
}

} // namespace core

} // namespace xmc
