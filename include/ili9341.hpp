#pragma once
// Treiber für ILI9341-Displays (240 x 320, RGB565) über SPI (SpiHw).
//
// Anschluss am Relax Kit (Arduino-Header):
//   VCC, LED -> 3V3     GND -> GND
//   SCK -> D13 (P3.9)   SDI/MOSI -> D11 (P3.8)   SDO/MISO -> D12 (P3.7, optional)
//   CS  -> D10 (P3.10)  DC -> D9 (P1.11)         RESET -> D8 (P1.10)
//
// Zeichnen: Flächen, Linien, Rechtecke, Pixel und Text (Font 6x10, skalierbar).
// Alle Funktionen blockieren, bis die Daten gesendet sind. Ein ganzer Bildschirm
// (153 600 Byte) dauert bei 20 MHz SPI ca. 62 ms.

#include "gpio.hpp"
#include "spi_hw.hpp"
#include <cstdint>

class Ili9341 {
public:
  /// RGB565 aus 8-Bit-Anteilen
  static constexpr std::uint16_t rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
    return static_cast<std::uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  }

  static constexpr std::uint16_t BLACK = 0x0000;
  static constexpr std::uint16_t WHITE = 0xFFFF;
  static constexpr std::uint16_t RED = 0xF800;
  static constexpr std::uint16_t GREEN = 0x07E0;
  static constexpr std::uint16_t BLUE = 0x001F;
  static constexpr std::uint16_t YELLOW = 0xFFE0;
  static constexpr std::uint16_t CYAN = 0x07FF;
  static constexpr std::uint16_t MAGENTA = 0xF81F;
  static constexpr std::uint16_t GRAY = 0x8410;

  /// `rst` darf nullptr sein (Reset-Leitung fest an 3,3 V); dann nur Software-Reset.
  Ili9341(SpiHw &spi, GpioOutput &cs, GpioOutput &dc, GpioOutput *rst) noexcept
      : spi_(spi), cs_(cs), dc_(dc), rst_(rst) {}

  /// Pins einrichten und Display initialisieren (blockiert ca. 300 ms; SysTick muss laufen).
  /// rotation: 0 = Hochformat, 1 = Querformat (Standard), 2/3 = jeweils um 180 ° gedreht.
  void init(std::uint8_t rotation = 1) noexcept;

  void set_rotation(std::uint8_t rotation) noexcept;
  /// Farbreihenfolge der Panels umschalten, falls Rot und Blau vertauscht erscheinen.
  void set_bgr(bool bgr) noexcept;

  std::int16_t width() const noexcept { return width_; }
  std::int16_t height() const noexcept { return height_; }

  void fill_screen(std::uint16_t color) noexcept { fill_rect(0, 0, width_, height_, color); }
  void fill_rect(std::int16_t x, std::int16_t y, std::int16_t w, std::int16_t h,
                 std::uint16_t color) noexcept;
  void draw_pixel(std::int16_t x, std::int16_t y, std::uint16_t color) noexcept {
    fill_rect(x, y, 1, 1, color);
  }
  void draw_hline(std::int16_t x, std::int16_t y, std::int16_t w, std::uint16_t color) noexcept {
    fill_rect(x, y, w, 1, color);
  }
  void draw_vline(std::int16_t x, std::int16_t y, std::int16_t h, std::uint16_t color) noexcept {
    fill_rect(x, y, 1, h, color);
  }
  /// Rechteck-Umriss
  void draw_rect(std::int16_t x, std::int16_t y, std::int16_t w, std::int16_t h,
                 std::uint16_t color) noexcept;
  /// Linie (Bresenham)
  void draw_line(std::int16_t x0, std::int16_t y0, std::int16_t x1, std::int16_t y1,
                 std::uint16_t color) noexcept;

  /// Zeichen deckend (Vorder- und Hintergrundfarbe), `scale` 1..4. Zeichenzelle
  /// 6 x 10 Pixel mal `scale`; `x`/`y` = linke obere Ecke.
  void draw_char(std::int16_t x, std::int16_t y, char c, std::uint16_t fg, std::uint16_t bg,
                 std::uint8_t scale = 1) noexcept;
  /// Text deckend ausgeben; gibt die x-Position hinter dem letzten Zeichen zurück.
  std::int16_t draw_text(std::int16_t x, std::int16_t y, const char *text, std::uint16_t fg,
                         std::uint16_t bg, std::uint8_t scale = 1) noexcept;

private:
  void command(std::uint8_t cmd, const std::uint8_t *data = nullptr, std::size_t n = 0) noexcept;
  /// Fenster setzen und RAMWR senden; danach bleibt CS aktiv und DC = Daten.
  void begin_pixels(std::int16_t x0, std::int16_t y0, std::int16_t x1, std::int16_t y1) noexcept;
  void end_pixels() noexcept { cs_.write(true); }
  void write_madctl() noexcept;

  SpiHw &spi_;
  GpioOutput &cs_;
  GpioOutput &dc_;
  GpioOutput *rst_;
  std::int16_t width_ = 320;
  std::int16_t height_ = 240;
  std::uint8_t rotation_ = 1;
  bool bgr_ = true;
};
