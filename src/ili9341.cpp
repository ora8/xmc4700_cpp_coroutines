#include "ili9341.hpp"
#include "executor.hpp" // delay_ms()
#include "font_6x10.hpp"

namespace {
// ILI9341-Kommandos
constexpr std::uint8_t SWRESET = 0x01;
constexpr std::uint8_t SLPOUT = 0x11;
constexpr std::uint8_t NORON = 0x13;
constexpr std::uint8_t INVOFF = 0x20;
constexpr std::uint8_t DISPON = 0x29;
constexpr std::uint8_t CASET = 0x2A;
constexpr std::uint8_t PASET = 0x2B;
constexpr std::uint8_t RAMWR = 0x2C;
constexpr std::uint8_t MADCTL = 0x36;
constexpr std::uint8_t COLMOD = 0x3A;

// MADCTL-Bits
constexpr std::uint8_t MADCTL_MY = 0x80;
constexpr std::uint8_t MADCTL_MX = 0x40;
constexpr std::uint8_t MADCTL_MV = 0x20;
constexpr std::uint8_t MADCTL_BGR = 0x08;
} // namespace

void Ili9341::command(std::uint8_t cmd, const std::uint8_t *data, std::size_t n) noexcept {
  cs_.write(false);
  dc_.write(false);
  spi_.write(&cmd, 1);
  dc_.write(true);
  if (n) spi_.write(data, n);
  cs_.write(true);
}

void Ili9341::init(std::uint8_t rotation) noexcept {
  cs_.configure();
  cs_.write(true);
  dc_.configure();
  dc_.write(true);
  if (rst_) {
    rst_->configure();
    rst_->write(true);
    delay_ms(5);
    rst_->write(false); // Hardware-Reset: mindestens 10 µs Low
    delay_ms(20);
    rst_->write(true);
    delay_ms(120);
  }
  command(SWRESET);
  delay_ms(150);
  command(SLPOUT); // Sleep Out: danach 120 ms warten
  delay_ms(120);
  const std::uint8_t pixel_format = 0x55; // 16 Bit/Pixel (RGB565) für SPI und RGB
  command(COLMOD, &pixel_format, 1);
  command(INVOFF);
  set_rotation(rotation);
  command(NORON);
  command(DISPON);
  delay_ms(20);
}

void Ili9341::write_madctl() noexcept {
  // Werte wie in den verbreiteten Arduino-Treibern (Adafruit_ILI9341)
  static constexpr std::uint8_t table[4] = {
      MADCTL_MX,                         // 0: Hochformat 240 x 320
      MADCTL_MV,                         // 1: Querformat 320 x 240
      MADCTL_MY,                         // 2: Hochformat, 180 ° gedreht
      MADCTL_MX | MADCTL_MY | MADCTL_MV, // 3: Querformat, 180 ° gedreht
  };
  const std::uint8_t value = table[rotation_] | (bgr_ ? MADCTL_BGR : 0);
  command(MADCTL, &value, 1);
}

void Ili9341::set_rotation(std::uint8_t rotation) noexcept {
  rotation_ = rotation & 3;
  const bool landscape = (rotation_ & 1) != 0;
  width_ = landscape ? 320 : 240;
  height_ = landscape ? 240 : 320;
  write_madctl();
}

void Ili9341::set_bgr(bool bgr) noexcept {
  bgr_ = bgr;
  write_madctl();
}

void Ili9341::begin_pixels(std::int16_t x0, std::int16_t y0, std::int16_t x1,
                           std::int16_t y1) noexcept {
  const std::uint8_t col[4] = {std::uint8_t(x0 >> 8), std::uint8_t(x0), std::uint8_t(x1 >> 8),
                               std::uint8_t(x1)};
  const std::uint8_t row[4] = {std::uint8_t(y0 >> 8), std::uint8_t(y0), std::uint8_t(y1 >> 8),
                               std::uint8_t(y1)};
  command(CASET, col, 4);
  command(PASET, row, 4);
  cs_.write(false);
  dc_.write(false);
  const std::uint8_t cmd = RAMWR;
  spi_.write(&cmd, 1);
  dc_.write(true); // ab hier Pixeldaten, CS bleibt aktiv
}

void Ili9341::fill_rect(std::int16_t x, std::int16_t y, std::int16_t w, std::int16_t h,
                        std::uint16_t color) noexcept {
  // Auf den Bildschirm beschneiden
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > width_) w = width_ - x;
  if (y + h > height_) h = height_ - y;
  if (w <= 0 || h <= 0) return;

  begin_pixels(x, y, x + w - 1, y + h - 1);
  const std::uint32_t pixels = std::uint32_t(w) * std::uint32_t(h);
  const auto hi = std::uint8_t(color >> 8);
  const auto lo = std::uint8_t(color);
  if (hi == lo) {
    // z. B. Schwarz/Weiß: ein Byte wiederholen, kein Puffer nötig
    spi_.write_repeated(hi, pixels * 2u);
  } else {
    std::uint8_t buf[128];
    for (std::size_t i = 0; i < sizeof buf; i += 2) {
      buf[i] = hi;
      buf[i + 1] = lo;
    }
    std::uint32_t left = pixels * 2u;
    while (left) {
      const std::size_t n = left < sizeof buf ? left : sizeof buf;
      spi_.write(buf, n);
      left -= n;
    }
  }
  end_pixels();
}

void Ili9341::draw_rect(std::int16_t x, std::int16_t y, std::int16_t w, std::int16_t h,
                        std::uint16_t color) noexcept {
  draw_hline(x, y, w, color);
  draw_hline(x, y + h - 1, w, color);
  draw_vline(x, y, h, color);
  draw_vline(x + w - 1, y, h, color);
}

void Ili9341::draw_line(std::int16_t x0, std::int16_t y0, std::int16_t x1, std::int16_t y1,
                        std::uint16_t color) noexcept {
  if (y0 == y1) { // waagrecht/senkrecht als Fläche (schneller)
    draw_hline(x0 < x1 ? x0 : x1, y0, std::int16_t((x0 < x1 ? x1 - x0 : x0 - x1) + 1), color);
    return;
  }
  if (x0 == x1) {
    draw_vline(x0, y0 < y1 ? y0 : y1, std::int16_t((y0 < y1 ? y1 - y0 : y0 - y1) + 1), color);
    return;
  }
  const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
  const int dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
  const int sx = x0 < x1 ? 1 : -1;
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    draw_pixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 = std::int16_t(x0 + sx); }
    if (e2 <= dx) { err += dx; y0 = std::int16_t(y0 + sy); }
  }
}

void Ili9341::draw_char(std::int16_t x, std::int16_t y, char c, std::uint16_t fg,
                        std::uint16_t bg, std::uint8_t scale) noexcept {
  if (scale < 1) scale = 1;
  if (scale > 4) scale = 4;
  const std::int16_t cw = std::int16_t(font6x10::width * scale);
  const std::int16_t ch = std::int16_t(font6x10::height * scale);
  // Nur ganz sichtbare Zeichen (einfaches Clipping)
  if (x < 0 || y < 0 || x + cw > width_ || y + ch > height_) return;
  if (c < font6x10::first || c > font6x10::last) c = '?';
  const std::uint8_t *glyph = font6x10::glyphs[c - font6x10::first];

  begin_pixels(x, y, x + cw - 1, y + ch - 1);
  std::uint8_t line[font6x10::width * 4 * 2]; // eine Pixelzeile, bis Skalierung 4
  for (unsigned row = 0; row < font6x10::height; ++row) {
    std::size_t n = 0;
    for (unsigned col = 0; col < font6x10::width; ++col) {
      const std::uint16_t color = (glyph[row] & (0x80u >> col)) ? fg : bg;
      for (unsigned s = 0; s < scale; ++s) {
        line[n++] = std::uint8_t(color >> 8);
        line[n++] = std::uint8_t(color);
      }
    }
    for (unsigned s = 0; s < scale; ++s) spi_.write(line, n);
  }
  end_pixels();
}

std::int16_t Ili9341::draw_text(std::int16_t x, std::int16_t y, const char *text,
                                std::uint16_t fg, std::uint16_t bg,
                                std::uint8_t scale) noexcept {
  const std::int16_t step = std::int16_t(font6x10::width * (scale < 1 ? 1 : scale > 4 ? 4 : scale));
  for (; *text; ++text) {
    draw_char(x, y, *text, fg, bg, scale);
    x = std::int16_t(x + step);
  }
  return x;
}
