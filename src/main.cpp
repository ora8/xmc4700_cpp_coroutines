#include "bme280.hpp"
#include "config.hpp"
#include "executor.hpp"
#include "gpio.hpp"
#include "i2c.hpp"
#include "ili9341.hpp"
#include "spi_hw.hpp"
#include "uart_hw.hpp"
#include "xpt2046.hpp"
#include "xmc_regs.hpp"
#include <cstdint>
#include <initializer_list>
#include <utility>

namespace {
GpioOutput led{board::port5_base, board::led_pin};
// Software-I2C: SDA=P3.15, SCL=P0.13, jeweils mit externem Pull-up.
I2c i2c{0x48028300UL, 15, 0x48028000UL, 13};
Bme280 bme{i2c, 0x76};
UartHw uart;
Executor executor;

// ILI9341 an SPI (USIC2: SCK D13, MOSI D11, MISO D12), CS D10, DC D9, RESET D8
SpiHw spi;
GpioOutput tft_cs{xmc::port::P3, 10};
GpioOutput tft_dc{xmc::port::P1, 11};
GpioOutput tft_rst{xmc::port::P1, 10};
Ili9341 tft{spi, tft_cs, tft_dc, &tft_rst};
constexpr std::uint8_t TFT_ROTATION = 1; // Querformat 320 x 240

// Touch-Controller XPT2046 am selben SPI-Bus: T_CS D4 (P1.8), T_IRQ D3 (P1.1)
GpioOutput touch_cs{xmc::port::P1, 8};
GpioInput touch_irq{xmc::port::P1, 1};
Xpt2046 touch{spi, touch_cs, &touch_irq};

// Mit J-Link/GDB beobachtbar. Bei fehlendem Sensor bleibt bme_ok=false.
volatile bool bme_ok = false;
volatile float bme_temperature_c = 0;
volatile float bme_humidity_percent = 0;
volatile float bme_pressure_hpa = 0;
} // namespace

Task blink_task() {
  for (;;) {
    led.write(!board::led_active_low);
    co_await Executor::sleep_for(500);
    led.write(board::led_active_low);
    co_await Executor::sleep_for(500);
  }
}

Task bme280_task() {
  for (;;) {
    if (!bme_ok) {
      // SDO an GND: 0x76, SDO an 3,3 V: 0x77
      for (std::uint8_t address : {std::uint8_t(0x76), std::uint8_t(0x77)}) {
        bme.set_address(address);
        bme_ok = bme.init();
        if (bme_ok)
          break;
      }
      if (!bme_ok) {
        uart.print("BME280: nicht gefunden (0x76/0x77)\n");
      } else {
        uart.print("BME280: initialisiert an 0x");
        uart.print(bme.address() == 0x76 ? "76\n" : "77\n");
      }
    }
    if (bme_ok) {
      bme_ok = bme.read();
      if (bme_ok) {
        bme_temperature_c = bme.temperature();
        bme_humidity_percent = bme.humidity();
        bme_pressure_hpa = bme.pressure();
        uart.print("Temperatur: ");
        uart.print_fixed(bme_temperature_c);
        uart.print(" C | Feuchte: ");
        uart.print_fixed(bme_humidity_percent);
        uart.print(" % | Druck: ");
        uart.print_fixed(bme_pressure_hpa);
        uart.print(" hPa\n");
      } else {
        uart.print("BME280: Lesefehler\n");
      }
    }
    co_await Executor::sleep_for(1000);
  }
}

// Zahl mit festen Nachkommastellen in einen Puffer schreiben (ohne printf)
char *format_fixed(char *out, float value, unsigned decimals) noexcept {
  if (value < 0) {
    *out++ = '-';
    value = -value;
  }
  std::uint32_t scale = 1;
  for (unsigned i = 0; i < decimals; ++i) scale *= 10;
  const auto rounded = static_cast<std::uint32_t>(value * float(scale) + 0.5f);
  char digits[12];
  unsigned n = 0;
  std::uint32_t whole = rounded / scale;
  do {
    digits[n++] = char('0' + whole % 10);
    whole /= 10;
  } while (whole);
  while (n) *out++ = digits[--n];
  if (decimals) {
    *out++ = '.';
    std::uint32_t frac = rounded % scale;
    for (unsigned i = 0; i < decimals; ++i) {
      scale /= 10;
      *out++ = char('0' + frac / scale);
      frac %= scale;
    }
  }
  *out = '\0';
  return out;
}

// Zeigt die BME280-Werte und die Laufzeit auf dem Display, jede Sekunde neu
Task display_task() {
  char line[32];
  for (;;) {
    const std::int16_t x = 10;
    if (bme_ok) {
      char *p = format_fixed(line, bme_temperature_c, 2);
      *p++ = ' ';
      *p++ = 'C';
      *p++ = ' ';
      *p = '\0';
      tft.draw_text(x, 70, "Temperatur", Ili9341::GRAY, Ili9341::BLACK, 2);
      tft.draw_text(x, 92, line, Ili9341::WHITE, Ili9341::BLACK, 3);
      p = format_fixed(line, bme_humidity_percent, 1);
      *p++ = ' ';
      *p++ = '%';
      *p++ = ' ';
      *p = '\0';
      tft.draw_text(x, 130, "Feuchte", Ili9341::GRAY, Ili9341::BLACK, 2);
      tft.draw_text(x + 150, 130, line, Ili9341::CYAN, Ili9341::BLACK, 2);
      p = format_fixed(line, bme_pressure_hpa, 1);
      *p++ = ' ';
      *p++ = 'h';
      *p++ = 'P';
      *p++ = 'a';
      *p++ = ' ';
      *p = '\0';
      tft.draw_text(x, 156, "Druck", Ili9341::GRAY, Ili9341::BLACK, 2);
      tft.draw_text(x + 150, 156, line, Ili9341::YELLOW, Ili9341::BLACK, 2);
    } else {
      tft.draw_text(x, 92, "BME280 fehlt  ", Ili9341::RED, Ili9341::BLACK, 2);
    }
    const auto s = millis() / 1000;
    char *p = format_fixed(line, float(s), 0);
    *p++ = ' ';
    *p++ = 's';
    *p = '\0';
    tft.draw_text(x, 200, "Laufzeit ", Ili9341::GRAY, Ili9341::BLACK, 2);
    tft.draw_text(x + 108, 200, line, Ili9341::WHITE, Ili9341::BLACK, 2);
    co_await Executor::sleep_for(1000);
  }
}

// Startbild: Titel und Farbbalken
void display_start_screen() noexcept {
  const auto t0 = millis();
  tft.fill_screen(Ili9341::BLACK);
  const auto t_clear = millis() - t0;
  tft.draw_text(10, 10, "XMC4700 + C++20", Ili9341::WHITE, Ili9341::BLACK, 3);
  const std::uint16_t colors[] = {Ili9341::RED,  Ili9341::GREEN, Ili9341::BLUE,
                                  Ili9341::YELLOW, Ili9341::CYAN, Ili9341::MAGENTA};
  const std::int16_t w = tft.width() / 6;
  for (unsigned i = 0; i < 6; ++i) {
    tft.fill_rect(std::int16_t(i * w), 44, w, 16, colors[i]);
  }
  tft.draw_rect(0, 0, tft.width(), tft.height(), Ili9341::GRAY);
  uart.print("Display: SPI ");
  uart.print_int(static_cast<std::int32_t>(spi.frequency() / 1000));
  uart.print(" kHz, Bildschirm loeschen ");
  uart.print_int(static_cast<std::int32_t>(t_clear));
  uart.print(" ms\n");
}

// Touch: alle 20 ms abfragen, Punkt malen, Koordinaten unten anzeigen.
// Pro Berührung einmal die Rohwerte über UART (zum Kalibrieren, siehe README).
Task touch_task() {
  bool was_touched = false;
  char line[40];
  for (;;) {
    Xpt2046::Raw raw;
    if (touch.touched() && touch.read_raw(raw)) {
      const auto p = touch.map(raw, TFT_ROTATION);
      tft.fill_rect(std::int16_t(p.x - 2), std::int16_t(p.y - 2), 5, 5, Ili9341::WHITE);
      char *q = line;
      for (const char *t = "Touch x="; *t;) *q++ = *t++;
      q = format_fixed(q, float(p.x), 0);
      for (const char *t = " y="; *t;) *q++ = *t++;
      q = format_fixed(q, float(p.y), 0);
      for (const char *t = "    "; *t;) *q++ = *t++;
      *q = '\0';
      tft.draw_text(10, 226, line, Ili9341::GREEN, Ili9341::BLACK, 1);
      if (!was_touched) {
        uart.print("Touch roh x=");
        uart.print_int(raw.x);
        uart.print(" y=");
        uart.print_int(raw.y);
        uart.print(" z=");
        uart.print_int(raw.z);
        uart.print(" -> Bildschirm ");
        uart.print_int(p.x);
        uart.print(",");
        uart.print_int(p.y);
        uart.print("\n");
      }
      was_touched = true;
    } else {
      was_touched = false;
    }
    co_await Executor::sleep_for(20);
  }
}

Task heartbeat_task() {
  for (;;) {
    co_await Executor::sleep_for(5000);
    uart.print("Laufzeit: ");
    uart.print_int(static_cast<std::int32_t>(millis() / 1000));
    uart.print(" s\n");
  }
}

static void systick_init() noexcept {
  using namespace xmc::core;
  xmc::reg(SYST_CSR) = 0;
  xmc::reg(SYST_RVR) = (CPU_HZ / 1000UL) - 1UL;
  xmc::reg(SYST_CVR) = 0;
  // CLKSOURCE = Prozessortakt, TICKINT, ENABLE
  xmc::reg(SYST_CSR) = 7;
}

static void vector_table_init() noexcept {
  xmc::reg(xmc::core::SCB_VTOR) = 0x08000000UL;
  xmc::core::dsb_isb();
}

static void spawn(Task &&task, const char *name) noexcept {
  if (!executor.spawn(std::move(task))) {
    // Coroutine-Frame passt nicht in den Pool (coroutine_pool.cpp) oder alle
    // Slots belegt
    uart.print("Fehler: Task ");
    uart.print(name);
    uart.print(" konnte nicht gestartet werden\n");
  }
}

int main() {
  vector_table_init();

  led.configure();
  led.write(board::led_active_low);

  if (uart.init(CPU_HZ)) {
    uart.print("\nXMC4700 C++20 Coroutines: gestartet, CPU ");
    uart.print_int(static_cast<std::int32_t>(CPU_HZ / 1000000UL));
    uart.print(" MHz\n");
  }

  i2c.init();
  systick_init();

  // Display: SPI 20 MHz (Datenblatt nennt 10 MHz, die Module laufen meist mit 20..40 MHz)
  SpiHw::Config spi_cfg;
  spi_cfg.frequency = 20'000'000;
  spi.init(CPU_HZ, spi_cfg);
  tft.init(TFT_ROTATION);
  display_start_screen();
  touch.init();

  spawn(blink_task(), "blink");
  spawn(bme280_task(), "bme280");
  spawn(heartbeat_task(), "heartbeat");
  spawn(display_task(), "display");
  spawn(touch_task(), "touch");

  executor.run();
}
