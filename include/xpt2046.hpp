#pragma once
// Touch-Treiber für den resistiven Touch-Controller XPT2046 (auf den meisten
// ILI9341-SPI-Modulen mit Touch, Pins T_CLK/T_CS/T_DIN/T_DO/T_IRQ).
//
// Anschluss am Relax Kit (gleicher SPI-Bus wie das Display):
//   T_CLK -> D13 (P3.9)   T_DIN -> D11 (P3.8)   T_DO -> D12 (P3.7)
//   T_CS  -> D4  (P1.8)   T_IRQ -> D3  (P1.1)
// Tipp: Den SDO/MISO-Pin des Displays nicht anschließen. Bei manchen Modulen gibt das
// ILI9341 die MISO-Leitung nicht frei und stört dann die Touch-Messung.
//
// Der XPT2046 verträgt höchstens ca. 2 MHz Takt. Der Treiber schaltet den Bus für jede
// Messung auf 2 MHz und danach auf den vorherigen Takt (Display) zurück.

#include "gpio.hpp"
#include "spi_hw.hpp"
#include <cstdint>

class Xpt2046 {
public:
  /// Rohwerte des Controllers (12 Bit) und Druck (größer = fester)
  struct Raw {
    std::uint16_t x, y, z;
  };
  /// Bildschirmkoordinaten
  struct Point {
    std::int16_t x, y;
    std::uint16_t z;
  };

  /// Zuordnung Rohwerte -> Hochformat-Koordinaten (240 x 320, wie Display-Rotation 0).
  /// Ab Werk unterscheiden sich die Module; Werte mit den Rohausgaben in den Ecken
  /// bestimmen (siehe README).
  struct Calibration {
    std::uint16_t x_min = 200, x_max = 3900; // Rohwert am linken / rechten Rand
    std::uint16_t y_min = 200, y_max = 3900; // Rohwert am oberen / unteren Rand
    bool swap_xy = false;                    // Rohachsen X und Y vertauscht
  };

  Xpt2046(SpiHw &spi, GpioOutput &cs, GpioInput *irq) noexcept : spi_(spi), cs_(cs), irq_(irq) {}

  void init() noexcept;
  void set_calibration(const Calibration &c) noexcept { cal_ = c; }
  /// Mindestdruck für eine gültige Berührung (Standard 300)
  void set_threshold(std::uint16_t z) noexcept { threshold_ = z; }

  /// Schnelle Abfrage über T_IRQ (low = berührt). Ohne IRQ-Pin: Druckmessung.
  bool touched() noexcept;
  /// Rohwerte messen; `false`, wenn nicht (fest genug) berührt.
  bool read_raw(Raw &raw) noexcept;
  /// Bildschirmkoordinaten passend zur Display-Rotation (0..3, wie Ili9341::init).
  bool read(Point &p, std::uint8_t rotation = 1) noexcept;

  /// Rohwerte in Bildschirmkoordinaten umrechnen (öffentlich zum Testen/Kalibrieren).
  Point map(const Raw &raw, std::uint8_t rotation) const noexcept;

private:
  std::uint16_t measure(std::uint8_t command) noexcept;
  std::uint16_t measure_filtered(std::uint8_t command) noexcept;

  SpiHw &spi_;
  GpioOutput &cs_;
  GpioInput *irq_;
  Calibration cal_{};
  std::uint16_t threshold_ = 300;
};
