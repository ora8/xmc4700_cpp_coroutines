#include "bme280.hpp"
#include "config.hpp"
#include "executor.hpp"
#include "gpio.hpp"
#include "i2c.hpp"
#include "uart_hw.hpp"
#include "xmc_regs.hpp"
#include <cstdint>
#include <utility>

namespace {
GpioOutput led{board::port5_base, board::led_pin};
// Software-I2C: SDA=P3.15, SCL=P0.13, jeweils mit externem Pull-up.
I2c i2c{0x48028300UL, 15, 0x48028000UL, 13};
Bme280 bme{i2c, 0x76};
UartHw uart;
Executor executor;

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

  spawn(blink_task(), "blink");
  spawn(bme280_task(), "bme280");
  spawn(heartbeat_task(), "heartbeat");

  executor.run();
}
