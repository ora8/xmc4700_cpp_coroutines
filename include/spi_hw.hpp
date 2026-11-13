#pragma once
// SPI-Master auf USIC2 Kanal 0 (SSC-Modus), blockierend, 8 Bit, MSB zuerst.
//
// Pins am Arduino-Header des XMC4700 Relax Kits (wie XMC-for-Arduino `SPI`):
//
//   Arduino  Signal  Pin    Funktion
//   D13      SCK     P3.9   ALT1 = U2C0.SCLKOUT
//   D11      MOSI    P3.8   ALT1 = U2C0.DOUT0
//   D12      MISO    P3.7   Eingang U2C0.DX0C
//
// Chip-Select (z. B. D10 = P3.10) und bei Displays DC/RESET sind normale GPIO-Ausgänge
// (GpioOutput). Konfiguration wie im am Board getesteten Rust-Treiber (embassy-xmc-2,
// spi.rs) und XMC_SPI_CH_Init der XMCLib.

#include <cstddef>
#include <cstdint>

class SpiHw {
public:
  /// SPI-Modus: Polarität (CPOL) und Phase (CPHA)
  enum class Mode : std::uint8_t {
    Mode0, // CPOL 0, CPHA 0 (Standard, z. B. ILI9341)
    Mode1, // CPOL 0, CPHA 1
    Mode2, // CPOL 1, CPHA 0
    Mode3, // CPOL 1, CPHA 1
  };

  struct Config {
    /// Höchster Takt in Hz; eingestellt wird der nächste erreichbare darunter
    /// (höchstens fPERIPH / 2 = 72 MHz).
    std::uint32_t frequency = 20'000'000;
    Mode mode = Mode::Mode0;
    /// MOSI intern auf den Empfänger zurückführen (Selbsttest ohne Verdrahtung)
    bool loopback = false;
  };

  /// USIC2 CH0 und die Pins P3.7/P3.8/P3.9 einrichten. `false`, wenn die Frequenz
  /// nicht erreichbar ist.
  bool init(std::uint32_t peripheral_hz, const Config &config) noexcept;
  bool init(std::uint32_t peripheral_hz) noexcept { return init(peripheral_hz, Config{}); }

  /// Tatsächlich eingestellter Takt in Hz
  std::uint32_t frequency() const noexcept { return frequency_; }

  /// Vollduplex: `n` Bytes senden und empfangen. `tx == nullptr` sendet 0x00,
  /// `rx == nullptr` verwirft die Empfangsdaten. Kehrt zurück, wenn alles gesendet ist.
  void transfer(const std::uint8_t *tx, std::uint8_t *rx, std::size_t n) noexcept;

  void write(const std::uint8_t *data, std::size_t n) noexcept { transfer(data, nullptr, n); }
  void read(std::uint8_t *data, std::size_t n) noexcept { transfer(nullptr, data, n); }

  /// Ein Byte senden und das gleichzeitig empfangene zurückgeben.
  std::uint8_t transfer(std::uint8_t byte) noexcept {
    std::uint8_t in = 0;
    transfer(&byte, &in, 1);
    return in;
  }

  /// Dasselbe Byte `n`-mal senden (z. B. Display-Fläche füllen).
  void write_repeated(std::uint8_t byte, std::size_t n) noexcept;

private:
  std::uint32_t frequency_ = 0;
};
