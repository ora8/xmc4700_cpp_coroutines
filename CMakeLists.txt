cmake_minimum_required(VERSION 3.20)
project(xmc4700_cpp_coroutines C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ohne Build-Typ übersetzt CMake ohne Optimierung (-O0): Die Coroutine-Frames werden dann
# größer als die 256-Byte-Blöcke des Pools, und Tasks starten stillschweigend nicht.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "Build-Typ" FORCE)
endif()

# SystemInit() stellt den Takt auf 144 MHz (PLL vom 12-MHz-Quarz), siehe src/startup.cpp.
set(CPU_HZ 144000000 CACHE STRING "Core clock in Hz")

# Gemeinsame Quellen aller Programme (ohne main)
set(COMMON_SOURCES
    src/executor.cpp
    src/gpio.cpp
    src/bme280.cpp
    src/i2c.cpp
    src/spi_hw.cpp
    src/ili9341.cpp
    src/xpt2046.cpp
    src/uart_hw.cpp
    src/coroutine_pool.cpp
    src/startup.cpp
)

# Gemeinsame Optionen für Cortex-M4F (Compiler und Linker)
set(MCU_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)

# LVGL aus einer lokalen Kopie (Git-Checkout), Konfiguration in include/lv_conf.h
set(LVGL_DIR "$ENV{HOME}/lvgl" CACHE PATH "Pfad zur LVGL-Quellkopie")
if(NOT EXISTS "${LVGL_DIR}/lvgl.h")
    message(FATAL_ERROR "LVGL nicht gefunden in ${LVGL_DIR} (cmake -DLVGL_DIR=...)")
endif()
file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS "${LVGL_DIR}/src/*.c")
add_library(lvgl STATIC ${LVGL_SOURCES})
target_include_directories(lvgl SYSTEM PUBLIC "${LVGL_DIR}")
target_compile_definitions(lvgl PUBLIC LV_CONF_PATH="${CMAKE_SOURCE_DIR}/include/lv_conf.h")
target_compile_options(lvgl PRIVATE ${MCU_FLAGS} -ffunction-sections -fdata-sections -w)

# Firmware-Ziel mit Compiler-/Linker-Optionen für den XMC4700 anlegen
function(xmc_firmware name)
    add_executable(${name} ${ARGN} ${COMMON_SOURCES})
    set_target_properties(${name} PROPERTIES SUFFIX ".elf")
    target_include_directories(${name} PRIVATE include)
    target_compile_definitions(${name} PRIVATE CPU_HZ=${CPU_HZ})
    target_compile_options(${name} PRIVATE
        ${MCU_FLAGS}
        -ffunction-sections
        -fdata-sections
        -fno-exceptions
        -fno-rtti
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_options(${name} PRIVATE
        ${MCU_FLAGS}
        -T${CMAKE_SOURCE_DIR}/linker/xmc4700.ld
        --specs=nano.specs
        --specs=nosys.specs
        -Wl,--gc-sections
        -Wl,-Map=${CMAKE_BINARY_DIR}/${name}.map
    )
endfunction()

# Hauptprogramm: Coroutine-Executor, LED, BME280, UART, LVGL-Oberfläche auf ILI9341 + Touch
xmc_firmware(xmc4700_coroutines src/main.cpp src/lvgl_port.cpp src/ui.cpp)
target_link_libraries(xmc4700_coroutines PRIVATE lvgl)

# SPI-Selbsttest ohne Verdrahtung (interne Rückschleife), Ausgabe über UART
xmc_firmware(spi_loopback src/tests/spi_loopback.cpp)
