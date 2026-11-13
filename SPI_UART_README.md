# SPI, UART + BME280 Serial Monitor

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

## UART + BME280

- `src/uart_hw.cpp` (`UartHw`): hardware UART on USIC0 channel 0, 115200 baud, 8N1. TX = P1.5, RX = P1.4, wired on the Relax Kit to the on-board J-Link virtual COM port: no adapter needed, open `/dev/ttyACM0` at 115200 8N1 (e.g. `picocom -b 115200 /dev/ttyACM0`).
- I2C BME280: SDA=P3.15, SCL=P0.13, 4.7k pullups to 3.3V; CSB=3.3V; SDO=GND (address 0x76) or 3.3V (0x77). `main.cpp` tries both addresses.
- The BME280 measurement task prints T / RH / pressure every second, or error messages.
- **Clock:** `SystemInit()` (`src/startup.cpp`) sets the system clock to 144 MHz (PLL from the 12 MHz crystal), matching `CPU_HZ` in `CMakeLists.txt`. The UART baud rate and the SysTick period are derived from `CPU_HZ`; if you change the clock setup, change `CPU_HZ` too.
- Verified on the board (13.11.2026): UART output and BME280 readings at 0x77; SPI self test.
