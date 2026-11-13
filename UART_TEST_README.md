# XMC4700 Relax Lite – USIC0_CH0 UART smoke test

This is an **experimental hardware UART test**, not a validated board image.

- P1.5 -> USIC0_CH0 DOUT0 -> on-board J-Link VCOM
- P1.4 -> USIC0_CH0 DX0B
- 115200 baud, 8N1
- BME280 intentionally not started; LED coroutine retained.
- The USIC peripheral clock is **assumed to be 144 MHz**. CPU_HZ does not set the clock: SystemInit is empty in the uploaded project.
- If the LED runs slowly, verify the real clock and update both CPU_HZ and `uart.init(...)`.

Build with `./build.sh`, flash with `./flash.sh`, then `picocom -b 115200 /dev/ttyACM0` and reset the board.

Expected test text: `XMC4700 UART OK 115200`.

No ARM cross-compiler or physical XMC4700 is available here. A successful build and UART transmission have **not** been verified.
