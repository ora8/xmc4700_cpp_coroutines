#pragma once
#include <cstddef>
#include <cstdint>

// Software-I2C, GPIO open drain, keine XMC/Arduino-Bibliothek.
// Pins muessen fuer die gewaehlte Platine frei sein.
class I2c {
public:
  I2c(std::uintptr_t sda_port, unsigned sda_pin, std::uintptr_t scl_port,
      unsigned scl_pin) noexcept;
  void init() noexcept;
  bool probe(std::uint8_t address) noexcept;
  bool write(std::uint8_t address, const std::uint8_t *data,
             std::size_t length) noexcept;
  bool write_read(std::uint8_t address, const std::uint8_t *tx,
                  std::size_t tx_len, std::uint8_t *rx,
                  std::size_t rx_len) noexcept;

private:
  std::uintptr_t sda_port_, scl_port_;
  unsigned sda_pin_, scl_pin_;
  static void delay() noexcept;
  static void drive_low(std::uintptr_t port, unsigned pin) noexcept;
  static void release(std::uintptr_t port, unsigned pin) noexcept;
  static bool read_pin(std::uintptr_t port, unsigned pin) noexcept;
  bool clock_high() noexcept;
  bool start() noexcept;
  void stop() noexcept;
  bool send(std::uint8_t byte) noexcept;
  std::uint8_t receive(bool ack) noexcept;
};
