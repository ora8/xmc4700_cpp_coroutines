#pragma once
// Anbindung von LVGL an die Treiber dieses Projekts:
//   Zeitbasis  millis() (SysTick)
//   Display    Ili9341::write_pixels() mit Teilpuffer (320 x 40 Pixel, 25,6 KB)
//   Eingabe    Xpt2046 als Zeiger (Touch), Rohwerte pro Berührung über UART
//   Log        LVGL-Warnungen über UART
//
// Ablauf ohne RTOS: lvgl_port::handler() regelmäßig aus einem Task aufrufen
// (siehe lvgl_task in main.cpp). Alle LVGL-Aufrufe aus demselben Ablauf (Executor).

#include "ili9341.hpp"
#include "uart_hw.hpp"
#include "xpt2046.hpp"
#include <cstdint>

namespace lvgl_port {

/// LVGL initialisieren. Das Display muss bereits mit `tft.init(rotation)` laufen.
/// `touch` und `log` dürfen nullptr sein.
void init(Ili9341 &tft, Xpt2046 *touch, std::uint8_t rotation, UartHw *log) noexcept;

/// lv_timer_handler() ausführen; gibt die Wartezeit bis zum nächsten Aufruf in ms zurück
/// (begrenzt auf 1..10 ms).
std::uint32_t handler() noexcept;

/// Sofort komplett neu zeichnen und die Dauer in ms zurückgeben (Messung beim Start).
std::uint32_t refresh_now() noexcept;

/// Freier und größter belegter Speicher im LVGL-Pool in Byte
void memory(std::uint32_t &free_bytes, std::uint32_t &max_used_bytes) noexcept;

} // namespace lvgl_port
