# SPI, display, touch, LVGL, UART + BME280 Serial Monitor

## SPI (`include/spi_hw.hpp`, `src/spi_hw.cpp`)

Hardware SPI master on USIC2 channel 0 (SSC mode), blocking, 8 bit, MSB first, wired to
the Arduino header like XMC-for-Arduino `SPI`:

| Arduino | Signal | Pin | Function |
|---|---|---|---|
| D13 | SCK | P3.9 | ALT1, U2C0.SCLKOUT |
| D11 | MOSI | P3.8 | ALT1, U2C0.DOUT0 |
| D12 | MISO | P3.7 | input U2C0.DX0C |

```cpp
SpiHw spi;
SpiHw::Config cfg;            // default 20 MHz, mode 0
spi.init(CPU_HZ, cfg);        // returns false if the frequency is not reachable
spi.write(data, len);         // also: read(), transfer(tx, rx, n), transfer(byte),
                              //       write_repeated(byte, n) e.g. to fill a display area
```

- Modes 0..3; `frequency` is an upper limit (max. fPERIPH / 2 = 72 MHz), `frequency()`
  returns the actual clock.
- Chip select (e.g. D10 = P3.10) and, for displays, DC and RESET are plain `GpioOutput`s.
  For an ILI9341 module: CS = D10 (P3.10), DC = D9 (P1.11), RESET = D8 (P1.10),
  VCC/LED = 3V3 (3.3 V logic, no level shifter needed).
- Self test without wiring (internal loopback): build target `spi_loopback`, flash
  `build/spi_loopback.elf`. Verified on the board: modes 0..3 at 1/10/20/40 MHz OK,
  2461 KiB/s at 20 MHz (bus without gaps).

## ILI9341 display (`include/ili9341.hpp`, `src/ili9341.cpp`)

Driver for ILI9341 modules (240 x 320, RGB565) on top of `SpiHw`. Wiring: VCC/LED -> 3V3,
GND, SCK D13 (P3.9), SDI/MOSI D11 (P3.8), CS D10 (P3.10), DC D9 (P1.11), RESET D8 (P1.10).

```cpp
Ili9341 tft{spi, cs, dc, &rst};   // GpioOutput for CS, DC, RESET (RESET may be nullptr)
tft.init(1);                      // rotation 0..3, 1 = landscape 320 x 240; blocks ~300 ms
tft.fill_screen(Ili9341::BLACK);
tft.fill_rect(x, y, w, h, color);  tft.draw_rect(...);  tft.draw_line(...);
tft.draw_text(x, y, "Text", fg, bg, scale);   // font 6x10, scale 1..4, opaque
```

- Colors are RGB565 (`Ili9341::rgb(r, g, b)`, constants `RED`, `GREEN`, ...). If red and blue
  appear swapped on your module, call `tft.set_bgr(false)`.
- Font: `include/font_6x10.hpp`, generated from the X11 font "6x10" (public domain) via the
  embedded-graphics bitmap.
- `main.cpp` now draws through LVGL (see below); the driver's own drawing functions remain
  available for programs without LVGL. Without a display connected it runs harmlessly
  into the void.
- Tested: on the board the SPI traffic runs (full screen clear 61 ms at 20 MHz = gap-free).
  The drawing logic was verified on the PC against a simulated display. **Not yet tested
  with a real panel.**

## XPT2046 touch (`include/xpt2046.hpp`, `src/xpt2046.cpp`)

Resistive touch controller found on most ILI9341 SPI modules with touch (pins T_CLK, T_CS,
T_DIN, T_DO, T_IRQ). It shares the SPI bus with the display:

| Module | Relax Kit | Note |
|--------|-----------|------|
| T_CLK  | D13 (P3.9)  | same as display SCK |
| T_DIN  | D11 (P3.8)  | same as display SDI/MOSI |
| T_DO   | D12 (P3.7)  | MISO |
| T_CS   | D4  (P1.8)  | own chip select |
| T_IRQ  | D3  (P1.1)  | low while touched (internal pull-up) |

Leave the display's **SDO/MISO unconnected**: on some modules the ILI9341 does not release
MISO and corrupts the touch readings.

```cpp
Xpt2046 touch{spi, touch_cs, &touch_irq};   // irq may be nullptr (then pressure polling)
touch.init();
Xpt2046::Point p;
if (touch.touched() && touch.read(p, TFT_ROTATION)) { /* p.x, p.y in screen coords */ }
```

- The XPT2046 allows at most ~2 MHz. Each measurement switches the bus to 2 MHz via
  `SpiHw::set_frequency()` and back to the display clock afterwards.
- Filtering: first sample discarded, 5 samples sorted, the middle three averaged; pressure
  is checked before and after the X/Y measurement (threshold `set_threshold()`, default 300).
- **Calibration:** the LVGL input driver (`src/lvgl_port.cpp`) prints the raw values once
  per touch over the UART. Touch the four corners, then set `Calibration{x_min, x_max, y_min, y_max, swap_xy}`
  for portrait orientation (rotation 0) with `touch.set_calibration(...)`. `x_min > x_max`
  mirrors an axis; `swap_xy` swaps the raw axes. The driver converts to the display rotation.
- In `main.cpp` LVGL polls the touch controller (every 33 ms) as its pointer input.
- Tested: `set_frequency` 20 MHz -> 2 MHz -> 20 MHz in the loopback test (`spi_loopback`);
  firmware runs on the board without touch hardware (no false touches). Bit extraction and
  mapping for all 4 rotations verified on the PC. **Not yet tested with a real panel.**

## LVGL (`include/lv_conf.h`, `src/lvgl_port.cpp`, `src/ui.cpp`)

LVGL is compiled from a local copy (default `~/lvgl`, currently 9.6.0-dev) as a static
library; another location: `cmake -DLVGL_DIR=/path/to/lvgl ...`. The configuration is
`include/lv_conf.h` (copied from `lv_conf_template.h`, changes: 48 KB memory pool, log level
WARN over UART, fonts Montserrat 14/20/28).

- **Port** (`lvgl_port.cpp`): tick = `millis()`, partial draw buffer 320 x 40 px (25.6 KB),
  flush swaps RGB565 bytes and sends them with `Ili9341::write_pixels()`, XPT2046 as
  pointer input, default group. No RTOS: `lvgl_task` calls `lvgl_port::handler()` every
  1..10 ms. All LVGL calls must come from executor tasks (same execution context).
- **Demo UI** (`ui.cpp`, hardware-independent): header with uptime, card with temperature,
  humidity (bar) and pressure, switch "LED blinkt" and a slider for the blink interval
  (100..1000 ms). `ui_task` updates the values once per second.
- Built-in fonts contain ASCII and the degree sign only - no umlauts (generate your own font
  with the LVGL font converter if needed).
- Size: about 366 KB flash and 78 KB RAM in total (LVGL pool 48 KB + draw buffer 25.6 KB).
- Tested on the board (without panel): all tasks start, no LVGL warnings, first full frame
  225 ms (about 60 ms of it SPI), 36 KB of the pool still free. Tested on the PC with the
  real `lvgl_port.cpp`/`ui.cpp`/`xpt2046.cpp` and simulated hardware: rendering correct,
  tapping the switch turns blinking off, tapping the slider sets the interval.
  **Not yet tested with a real panel.**

## UART + BME280

- `src/uart_hw.cpp` (`UartHw`): hardware UART on USIC0 channel 0, 115200 baud, 8N1. TX = P1.5, RX = P1.4, wired on the Relax Kit to the on-board J-Link virtual COM port: no adapter needed, open `/dev/ttyACM0` at 115200 8N1 (e.g. `picocom -b 115200 /dev/ttyACM0`).
- I2C BME280: SDA=P3.15, SCL=P0.13, 4.7k pullups to 3.3V; CSB=3.3V; SDO=GND (address 0x76) or 3.3V (0x77). `main.cpp` tries both addresses.
- The BME280 measurement task prints T / RH / pressure every second, or error messages.
- **Clock:** `SystemInit()` (`src/startup.cpp`) sets the system clock to 144 MHz (PLL from the 12 MHz crystal), matching `CPU_HZ` in `CMakeLists.txt`. The UART baud rate and the SysTick period are derived from `CPU_HZ`; if you change the clock setup, change `CPU_HZ` too.
- Verified on the board (13.11.2026): UART output and BME280 readings at 0x77; SPI self test.
