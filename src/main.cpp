#include "bme280.hpp"
#include "config.hpp"
#include "executor.hpp"
#include "gpio.hpp"
#include "i2c.hpp"
#include "ili9341.hpp"
#include "lvgl_port.hpp"
#include "spi_hw.hpp"
#include "uart_hw.hpp"
#include "ui.hpp"
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

// Von der Oberfläche (ui.cpp) eingestellt
volatile bool led_blinking = true;
volatile std::uint32_t led_half_period_ms = 500;
} // namespace

// Blinkt im eingestellten Intervall; Schalter und Schieberegler der Oberfläche steuern es
Task blink_task() {
  for (;;) {
    if (led_blinking) led.write(!board::led_active_low);
    co_await Executor::sleep_for(led_half_period_ms);
    led.write(board::led_active_low);
    co_await Executor::sleep_for(led_half_period_ms);
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

// LVGL regelmäßig bedienen: Timer, Touch-Abfrage und Neuzeichnen geänderter Bereiche
Task lvgl_task() {
  for (;;) {
    co_await Executor::sleep_for(lvgl_port::handler());
  }
}

// Oberfläche jede Sekunde mit Messwerten und Laufzeit aktualisieren
Task ui_task() {
  for (;;) {
    ui::set_sensor(bme_ok, bme_temperature_c, bme_humidity_percent, bme_pressure_hpa);
    ui::set_uptime(millis() / 1000);
    co_await Executor::sleep_for(1000);
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
  touch.init();

  // LVGL und Oberfläche; erstes Bild sofort zeichnen und Dauer melden
  lvgl_port::init(tft, &touch, TFT_ROTATION, &uart);
  ui::Callbacks ui_cb;
  ui_cb.led_enable = [](bool on) { led_blinking = on; };
  ui_cb.blink_period = [](std::uint32_t ms) { led_half_period_ms = ms; };
  ui::create(ui_cb, led_half_period_ms);
  const auto frame_ms = lvgl_port::refresh_now();
  std::uint32_t mem_free = 0, mem_max = 0;
  lvgl_port::memory(mem_free, mem_max);
  uart.print("LVGL: SPI ");
  uart.print_int(static_cast<std::int32_t>(spi.frequency() / 1000));
  uart.print(" kHz, erstes Bild ");
  uart.print_int(static_cast<std::int32_t>(frame_ms));
  uart.print(" ms, Speicher frei ");
  uart.print_int(static_cast<std::int32_t>(mem_free));
  uart.print(" B, max. belegt ");
  uart.print_int(static_cast<std::int32_t>(mem_max));
  uart.print(" B\n");

  spawn(blink_task(), "blink");
  spawn(bme280_task(), "bme280");
  spawn(heartbeat_task(), "heartbeat");
  spawn(lvgl_task(), "lvgl");
  spawn(ui_task(), "ui");

  executor.run();
}
