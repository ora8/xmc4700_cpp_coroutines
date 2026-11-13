#include "xpt2046.hpp"

namespace {
// Steuerbyte: Start-Bit, Kanal A2..A0, 12 Bit, differentiell, PD = 00
// (zwischen den Messungen stromsparend, PENIRQ bleibt aktiv)
constexpr std::uint8_t CMD_X = 0xD0;  // A = 101
constexpr std::uint8_t CMD_Y = 0x90;  // A = 001
constexpr std::uint8_t CMD_Z1 = 0xB0; // A = 011
constexpr std::uint8_t CMD_Z2 = 0xC0; // A = 100

constexpr std::uint32_t TOUCH_SPI_HZ = 2'000'000;
constexpr unsigned SAMPLES = 5;

std::int32_t clamp(std::int32_t v, std::int32_t lo, std::int32_t hi) noexcept {
  return v < lo ? lo : v > hi ? hi : v;
}

/// Rohwert linear auf 0..size-1 abbilden
std::int16_t scale(std::uint16_t raw, std::uint16_t lo, std::uint16_t hi,
                   std::int32_t size) noexcept {
  if (hi == lo) return 0;
  const std::int32_t v = (std::int32_t(raw) - lo) * (size - 1) / (std::int32_t(hi) - lo);
  return std::int16_t(clamp(v, 0, size - 1));
}
} // namespace

void Xpt2046::init() noexcept {
  cs_.configure();
  cs_.write(true);
  if (irq_) irq_->configure(true);
  // Eine Messung mit PD = 00 schaltet PENIRQ ein
  const std::uint32_t prev = spi_.frequency();
  spi_.set_frequency(TOUCH_SPI_HZ);
  (void)measure(CMD_Y);
  spi_.set_frequency(prev);
}

std::uint16_t Xpt2046::measure(std::uint8_t command) noexcept {
  // 1 Byte Kommando, danach 2 Byte mit dem 12-Bit-Ergebnis (MSB zuerst, 3 Füllbits)
  const std::uint8_t tx[3] = {command, 0, 0};
  std::uint8_t rx[3] = {};
  cs_.write(false);
  spi_.transfer(tx, rx, 3);
  cs_.write(true);
  return std::uint16_t(((rx[1] << 8) | rx[2]) >> 3);
}

std::uint16_t Xpt2046::measure_filtered(std::uint8_t command) noexcept {
  // Erste Messung verwerfen (Einschwingen), dann sortieren und die mittleren mitteln
  (void)measure(command);
  std::uint16_t v[SAMPLES];
  for (auto &s : v) s = measure(command);
  for (unsigned i = 1; i < SAMPLES; ++i) {
    for (unsigned j = i; j > 0 && v[j - 1] > v[j]; --j) {
      const std::uint16_t t = v[j];
      v[j] = v[j - 1];
      v[j - 1] = t;
    }
  }
  return std::uint16_t((v[1] + v[2] + v[3]) / 3);
}

bool Xpt2046::touched() noexcept {
  if (irq_) return !irq_->read();
  Raw r;
  return read_raw(r);
}

bool Xpt2046::read_raw(Raw &raw) noexcept {
  const std::uint32_t prev = spi_.frequency();
  spi_.set_frequency(TOUCH_SPI_HZ);
  const std::uint16_t z1 = measure(CMD_Z1);
  const std::uint16_t z2 = measure(CMD_Z2);
  const std::int32_t z = std::int32_t(z1) + 4095 - std::int32_t(z2);
  bool pressed = z >= threshold_;
  if (pressed) {
    raw.x = measure_filtered(CMD_X);
    raw.y = measure_filtered(CMD_Y);
    raw.z = std::uint16_t(z);
    // Nochmals prüfen: Wurde während der Messung losgelassen, sind X/Y unbrauchbar
    const std::int32_t z_after = std::int32_t(measure(CMD_Z1)) + 4095 - measure(CMD_Z2);
    pressed = z_after >= threshold_;
  }
  spi_.set_frequency(prev);
  return pressed;
}

Xpt2046::Point Xpt2046::map(const Raw &raw, std::uint8_t rotation) const noexcept {
  const std::uint16_t rx = cal_.swap_xy ? raw.y : raw.x;
  const std::uint16_t ry = cal_.swap_xy ? raw.x : raw.y;
  // Hochformat-Koordinaten (Rotation 0, 240 x 320). x_min > x_max spiegelt die Achse.
  const std::int16_t px = scale(rx, cal_.x_min, cal_.x_max, 240);
  const std::int16_t py = scale(ry, cal_.y_min, cal_.y_max, 320);
  // In die Display-Rotation umrechnen (passend zu den MADCTL-Werten in ili9341.cpp)
  Point p{px, py, raw.z};
  switch (rotation & 3) {
  case 0: p.x = px; p.y = py; break;
  case 1: p.x = py; p.y = std::int16_t(239 - px); break;
  case 2: p.x = std::int16_t(239 - px); p.y = std::int16_t(319 - py); break;
  case 3: p.x = std::int16_t(319 - py); p.y = px; break;
  }
  return p;
}

bool Xpt2046::read(Point &p, std::uint8_t rotation) noexcept {
  Raw raw;
  if (!read_raw(raw)) return false;
  p = map(raw, rotation);
  return true;
}
