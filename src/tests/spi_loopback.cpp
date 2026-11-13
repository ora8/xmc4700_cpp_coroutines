// SPI-Selbsttest ohne Verdrahtung: MOSI wird im USIC intern auf den Empfänger
// zurückgeführt. Ergebnis auf dem Serial Monitor (/dev/ttyACM0, 115200 Baud).
//
// Bauen und flashen:  ./build.sh, dann build/spi_loopback.elf statt
// build/xmc4700_coroutines.elf flashen (z. B. in flash.jlink).

#include "config.hpp"
#include "executor.hpp"
#include "spi_hw.hpp"
#include "uart_hw.hpp"
#include "xmc_regs.hpp"
#include <cstdint>

namespace {
UartHw uart;
SpiHw spi;

void systick_init() noexcept {
  using namespace xmc::core;
  xmc::reg(SYST_CSR) = 0;
  xmc::reg(SYST_RVR) = (CPU_HZ / 1000UL) - 1UL;
  xmc::reg(SYST_CVR) = 0;
  xmc::reg(SYST_CSR) = 7;
}

const char *mode_name(SpiHw::Mode m) noexcept {
  switch (m) {
  case SpiHw::Mode::Mode0: return "0";
  case SpiHw::Mode::Mode1: return "1";
  case SpiHw::Mode::Mode2: return "2";
  case SpiHw::Mode::Mode3: return "3";
  }
  return "?";
}
} // namespace

int main() {
  xmc::reg(xmc::core::SCB_VTOR) = 0x08000000UL;
  xmc::core::dsb_isb();
  uart.init(CPU_HZ);
  systick_init();
  uart.print("\nSPI-Selbsttest (USIC2 CH0, interne Rueckschleife)\n");

  static std::uint8_t tx[256], rx[256], block[1024];
  for (unsigned i = 0; i < sizeof tx; ++i) {
    tx[i] = static_cast<std::uint8_t>((i * 37u) ^ 0xA5u);
  }
  for (auto &b : block) b = 0x55;

  unsigned errors = 0;
  for (auto mode : {SpiHw::Mode::Mode0, SpiHw::Mode::Mode1, SpiHw::Mode::Mode2,
                    SpiHw::Mode::Mode3}) {
    for (std::uint32_t freq : {1'000'000u, 10'000'000u, 20'000'000u, 40'000'000u}) {
      SpiHw::Config cfg;
      cfg.frequency = freq;
      cfg.mode = mode;
      cfg.loopback = true;
      if (!spi.init(CPU_HZ, cfg)) {
        uart.print("init fehlgeschlagen\n");
        ++errors;
        continue;
      }
      for (auto &b : rx) b = 0;
      spi.transfer(tx, rx, sizeof tx);
      bool ok = true;
      for (unsigned i = 0; i < sizeof tx; ++i) ok = ok && rx[i] == tx[i];
      if (!ok) ++errors;

      // Durchsatz: 32 KiB schreiben
      const auto t0 = millis();
      for (int k = 0; k < 32; ++k) spi.write(block, sizeof block);
      const auto ms = millis() - t0;

      uart.print("Modus ");
      uart.print(mode_name(mode));
      uart.print(", Soll ");
      uart.print_int(static_cast<std::int32_t>(freq / 1000));
      uart.print(" kHz, Ist ");
      uart.print_int(static_cast<std::int32_t>(spi.frequency() / 1000));
      uart.print(" kHz: ");
      uart.print(ok ? "OK" : "FEHLER");
      uart.print(", ");
      uart.print_int(ms ? static_cast<std::int32_t>(32u * 1000u / ms) : 0);
      uart.print(" KiB/s\n");
    }
  }
  // Takt zur Laufzeit umschalten (Display 20 MHz <-> Touch 2 MHz)
  {
    SpiHw::Config cfg;
    cfg.frequency = 20'000'000;
    cfg.loopback = true;
    spi.init(CPU_HZ, cfg);
    for (std::uint32_t f : {2'000'000u, 20'000'000u, 2'000'000u}) {
      const bool set_ok = spi.set_frequency(f);
      for (auto &b : rx) b = 0;
      spi.transfer(tx, rx, sizeof tx);
      bool ok = set_ok;
      for (unsigned i = 0; i < sizeof tx; ++i) ok = ok && rx[i] == tx[i];
      if (!ok) ++errors;
      uart.print("set_frequency ");
      uart.print_int(static_cast<std::int32_t>(f / 1000));
      uart.print(" kHz -> ");
      uart.print_int(static_cast<std::int32_t>(spi.frequency() / 1000));
      uart.print(" kHz: ");
      uart.print(ok ? "OK\n" : "FEHLER\n");
    }
  }
  uart.print(errors ? "Ergebnis: FEHLER\n" : "Ergebnis: alles OK\n");
  for (;;) {
    __asm volatile("wfi");
  }
}
