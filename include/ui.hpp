#pragma once
// Demo-Oberfläche mit LVGL (320 x 240): BME280-Werte, Laufzeit, LED-Schalter und
// Schieberegler für das Blinkintervall. Hängt nur von LVGL ab (auch am PC testbar).

#include <cstdint>

namespace ui {

struct Callbacks {
  void (*led_enable)(bool on) = nullptr;           // Schalter "LED blinkt"
  void (*blink_period)(std::uint32_t ms) = nullptr; // Schieberegler, 100..1000 ms
};

/// Oberfläche auf dem aktiven Bildschirm aufbauen (nach lvgl_port::init).
void create(const Callbacks &cb, std::uint32_t blink_period_ms) noexcept;

/// Messwerte anzeigen; `ok == false` zeigt "BME280 fehlt".
void set_sensor(bool ok, float temperature_c, float humidity_pct, float pressure_hpa) noexcept;
void set_uptime(std::uint32_t seconds) noexcept;

} // namespace ui
