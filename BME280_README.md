# BME280-Beispiel (XMC4700 / C++20 Coroutines)

## Anschlüsse (Software-I2C)
- SDA: P3.15
- SCL: P0.13
- GND: GND
- VCC: 3,3 V
- CSB: 3,3 V (I2C-Betrieb)
- SDO: GND (Adresse 0x76); an 3,3 V = 0x77 (dann Adresse in main.cpp ändern)
- SDA/SCL: externe Pull-ups, z.B. je 4,7 kOhm an 3,3 V.

**Achtung:** Dies ist GPIO-Bitbanging, keine USIC-Hardware-I2C-Implementierung. Die GPIO-Pins müssen tatsächlich frei und am Kit zugänglich sein. Das I2C-Timing ist grob und vom CPU-Takt abhängig. Der GPIO-Buszugriff blockiert kurzzeitig; nur das 1-s-Intervall nutzt `co_await`.

## Messwerte im Debugger
Die globalen Variablen (in anonymem Namespace) sind in `main.cpp`:
- `bme_ok`
- `bme_temperature_c`
- `bme_humidity_percent`
- `bme_pressure_hpa`

Falls Symbolzugriff auf die anonymen Variablen schwierig ist, `arm-none-eabi-nm -C build/xmc4700_coroutines.elf | grep bme_` verwenden.

## Bauen
```
./build.sh
```

Der Build wurde hier **nicht** ausgeführt: `arm-none-eabi-g++` ist in dieser Umgebung nicht installiert. Bitte auf dem Zielrechner bauen und die Meldungen bei Bedarf schicken.

Die alte `src/spi.cpp` enthält PIC-spezifische Bezeichner (`PORTAbits`) und wurde daher aus CMake entfernt, bleibt aber unverändert im ZIP.
