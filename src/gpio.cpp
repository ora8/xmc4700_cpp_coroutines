#include "gpio.hpp"
#include "xmc_regs.hpp"

using namespace xmc;

void GpioOutput::configure() noexcept {
  port::set_pin_mode(port_, pin_, port::OUTPUT_PUSH_PULL);
}

void GpioOutput::write(bool high) noexcept {
  if (pin_ > 15) {
    return;
  }
  port::write_pin(port_, pin_, high);
}

void GpioOutput::toggle() noexcept {
  if (pin_ > 15) {
    return;
  }
  const bool current = (reg(port_ + port::OUT) & (1u << pin_)) != 0;
  write(!current);
}

void GpioInput::configure(bool pull_up) noexcept {
  port::set_pin_mode(port_, pin_, pull_up ? port::INPUT_PULL_UP : port::INPUT_TRISTATE);
}

bool GpioInput::read() const noexcept {
  return pin_ <= 15 && port::read_pin(port_, pin_);
}
