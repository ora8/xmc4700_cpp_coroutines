// I2C nutzt dieselbe GPIO-Registerbasis, aber Open-Drain-Verhalten
// wird durch Umschalten zwischen Eingang (freigegeben) und Ausgang LOW
// erreicht.
#include "i2c.hpp"

#include "xmc_regs.hpp"

I2c::I2c(std::uintptr_t sda_port, unsigned sda_pin, std::uintptr_t scl_port,
         unsigned scl_pin) noexcept
    : sda_port_(sda_port), scl_port_(scl_port), sda_pin_(sda_pin),
      scl_pin_(scl_pin) {}

void I2c::delay() noexcept {
  // Konservative GPIO-I2C-Taktung; bei abweichendem CPU-Takt anpassen.
  for (unsigned i = 0; i < 450; ++i) {
    __asm volatile("nop");
  }
}

void I2c::drive_low(std::uintptr_t port, unsigned pin) noexcept {
  xmc::port::write_pin(port, pin, false);
  xmc::port::set_pin_mode(port, pin, xmc::port::OUTPUT_PUSH_PULL);
}

void I2c::release(std::uintptr_t port, unsigned pin) noexcept {
  // Hi-Z; externer Pull-up zwingend erforderlich
  xmc::port::set_pin_mode(port, pin, xmc::port::INPUT_TRISTATE);
}

bool I2c::read_pin(std::uintptr_t port, unsigned pin) noexcept {
  return xmc::port::read_pin(port, pin);
}

void I2c::init() noexcept {
  release(sda_port_, sda_pin_);
  release(scl_port_, scl_pin_);
}

bool I2c::clock_high() noexcept {
  release(scl_port_, scl_pin_);
  for (unsigned i = 0; i < 10000; ++i) {
    if (read_pin(scl_port_, scl_pin_)) {
      delay();
      return true;
    }
  }
  return false;
}

bool I2c::start() noexcept {
  release(sda_port_, sda_pin_);
  if (!clock_high() || !read_pin(sda_port_, sda_pin_))
    return false;
  drive_low(sda_port_, sda_pin_);
  delay();
  drive_low(scl_port_, scl_pin_);
  delay();
  return true;
}

void I2c::stop() noexcept {
  drive_low(sda_port_, sda_pin_);
  delay();
  (void)clock_high();
  release(sda_port_, sda_pin_);
  delay();
}

bool I2c::send(std::uint8_t byte) noexcept {
  for (unsigned i = 0; i < 8; ++i) {
    if (byte & 0x80)
      release(sda_port_, sda_pin_);
    else
      drive_low(sda_port_, sda_pin_);
    delay();
    if (!clock_high())
      return false;
    drive_low(scl_port_, scl_pin_);
    delay();
    byte <<= 1;
  }
  release(sda_port_, sda_pin_);
  delay();
  if (!clock_high())
    return false;
  const bool ack = !read_pin(sda_port_, sda_pin_);
  drive_low(scl_port_, scl_pin_);
  delay();
  return ack;
}

std::uint8_t I2c::receive(bool ack) noexcept {
  std::uint8_t value = 0;
  release(sda_port_, sda_pin_);
  for (unsigned i = 0; i < 8; ++i) {
    delay();
    value <<= 1;
    if (clock_high() && read_pin(sda_port_, sda_pin_))
      value |= 1;
    drive_low(scl_port_, scl_pin_);
    delay();
  }
  if (ack)
    drive_low(sda_port_, sda_pin_);
  else
    release(sda_port_, sda_pin_);
  delay();
  (void)clock_high();
  drive_low(scl_port_, scl_pin_);
  delay();
  release(sda_port_, sda_pin_);
  return value;
}

bool I2c::probe(std::uint8_t address) noexcept {
  if (address > 0x7f || !start())
    return false;
  const bool ok = send(static_cast<std::uint8_t>(address << 1));
  stop();
  return ok;
}

bool I2c::write(std::uint8_t address, const std::uint8_t *data,
                std::size_t length) noexcept {
  if (address > 0x7f || !start())
    return false;
  bool ok = send(static_cast<std::uint8_t>(address << 1));
  for (std::size_t i = 0; ok && i < length; ++i)
    ok = send(data[i]);
  stop();
  return ok;
}

bool I2c::write_read(std::uint8_t address, const std::uint8_t *tx,
                     std::size_t tx_len, std::uint8_t *rx,
                     std::size_t rx_len) noexcept {
  if (address > 0x7f || !start())
    return false;
  bool ok = send(static_cast<std::uint8_t>(address << 1));
  for (std::size_t i = 0; ok && i < tx_len; ++i)
    ok = send(tx[i]);
  if (ok && rx_len) {
    // Repeated START: SDA freigeben, waehrend SCL LOW ist.
    release(sda_port_, sda_pin_);
    delay();
    ok = start() && send(static_cast<std::uint8_t>((address << 1) | 1u));
    for (std::size_t i = 0; ok && i < rx_len; ++i)
      rx[i] = receive(i + 1 < rx_len);
  }
  stop();
  return ok;
}
