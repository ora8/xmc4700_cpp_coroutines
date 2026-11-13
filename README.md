# XMC4700 C++20 Coroutine Executor (minimal bare-metal starter)

A small, allocation-free cooperative executor for the Infineon XMC4700 Relax Kit.
It demonstrates C++20 coroutines, a millisecond SysTick clock, delay awaitables,
and direct GPIO register access. It does **not** use an RTOS, Arduino, or the
Infineon XMC peripheral library.

> Hardware note: this is a starter project, not a board-validated release.
> The LED example defaults to P5.8 as discussed. Confirm the LED net and active
> polarity against your exact Relax Kit revision before connecting hardware.
> The project deliberately leaves clock/PLL setup to the board startup: set
> `CPU_HZ` to the actual core clock used by your startup code.

## Korrekturen (13.11.2026, am Board getestet)

Das Programm blinkte, gab aber nichts über den UART aus. Behoben:

- **UART (`src/uart_hw.cpp`)**: USIC0 wurde nie getaktet und aus dem Reset geholt
  (`CGATCLR0`/`PRCLR0`); gewartet wurde auf Bit 11 statt `TCSR.TDV` (Bit 7); `TDSSM` und
  `PDL` fehlten. Baudrate jetzt aus dem tatsächlichen Takt berechnet (115200, J-Link-VCOM).
- **Takt (`src/startup.cpp`)**: `SystemInit()` war leer, der Chip lief am ungenauen internen
  Oszillator (gemessen ca. 36 MHz statt 144). Jetzt PLL 144 MHz vom 12-MHz-Quarz, passend
  zu `CPU_HZ`.
- **Sleep-Takt**: `SLEEPCR.SYSSEL` auf PLL. Sonst schaltet jedes `WFI` des Executors auf fOFI
  um: Die letzten UART-Bytes einer Zeile wurden zu Zeichensalat, SysTick lief im Schlaf falsch.
- **FPU** wird eingeschaltet (`CPACR`); ohne das stürzt die erste Gleitkomma-Operation ab.
- **`UNALIGN_TRP`**, vom Boot-ROM gesetzt, wird gelöscht.
- **Linker-Skript**: `.init_array` fehlte, die Konstruktoren globaler Objekte (`I2c`,
  `Bme280`, …) liefen nie.
- **Build**: Standard ist jetzt `MinSizeRel`; mit `-O0` passen die Coroutine-Frames nicht in
  die 256-Byte-Blöcke des Pools.
- **`main.cpp`**: Executor, SysTick und Tasks wieder aktiv; BME280 an 0x76 oder 0x77.

Ausgabe (picocom/Serial Monitor an `/dev/ttyACM0`, 115200 Baud): Startmeldung, jede Sekunde
Temperatur/Feuchte/Druck des BME280, alle 5 s die Laufzeit. Am Board geprüft: Ausgabe ohne
Fehlzeichen, BME280 an 0x77, Laufzeit gegen PC-Uhr 15 s / 15,00 s.

## Requirements

- `arm-none-eabi-gcc` with C++20 coroutine support (`__cpp_impl_coroutine`)
- CMake 3.20+
- Ninja or Make
- A startup/system initialization appropriate for your XMC4700 board, or use
  the minimal startup and linker script included here as a starting point.

## Build

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
  -DCPU_HZ=120000000
cmake --build build
```

The `CPU_HZ` value must match the clock configured by the startup code. The
minimal startup supplied here does not configure the PLL; it assumes the reset
clock is already the frequency you specify. For a production board project,
replace `SystemInit()` with the clock setup for your board revision.

## Flash

Use a J-Link or the programmer/debugger configured for your board. Example:

```sh
JLinkExe -device XMC4700-F144x2048 -if SWD -speed 4000 -autoconnect 1
# At the J-Link prompt:
# r
# loadfile build/xmc4700_coroutines.elf
# r
# g
```

Check the exact device name/package printed on your kit before programming.

## Design

- `Task` is a lazy coroutine return type.
- Coroutine frames come from a fixed-size static pool (no heap).
- `Executor` owns a fixed number of task slots and resumes ready tasks.
- `sleep_for()` suspends a coroutine until the SysTick millisecond counter
  reaches its deadline.
- The main loop calls `run_once()` and uses `WFI` while no task is ready.

The executor is cooperative: a coroutine must not block for long periods.
This first version has one executor context and is not thread-safe. Interrupt
handlers should only update small state/flags; do not resume coroutines from an
ISR in this design.
