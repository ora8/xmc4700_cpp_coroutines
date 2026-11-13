#include "executor.hpp"
#include <cstddef>

namespace {
volatile std::uint32_t tick_ms = 0;
Executor *active_executor = nullptr;
} // namespace

extern "C" void SysTick_Handler() { tick_ms = tick_ms + 1; }

std::uint32_t millis() noexcept { return tick_ms; }

bool Executor::spawn(Task &&task) noexcept {
  if (!task)
    return false;
  for (auto &slot : slots_) {
    if (!slot.handle) {
      slot.handle = task.release();
      slot.waiting = false;
      slot.wake_at = 0;
      return true;
    }
  }
  return false;
}

void Executor::SleepAwaiter::await_suspend(std::coroutine_handle<> h) noexcept {
  if (!active_executor)
    return;
  const auto now = millis();
  for (auto &slot : active_executor->slots_) {
    if (slot.handle && slot.handle.address() == h.address()) {
      slot.wake_at = now + delay;
      slot.waiting = true;
      return;
    }
  }
}

void Executor::run_once() noexcept {
  active_executor = this;
  const auto now = millis();
  for (auto &slot : slots_) {
    if (!slot.handle)
      continue;
    if (slot.waiting && static_cast<std::int32_t>(now - slot.wake_at) < 0)
      continue;
    slot.waiting = false;
    slot.handle.resume();
    if (slot.handle.done()) {
      slot.handle.destroy();
      slot = {};
    }
  }
}

[[noreturn]] void Executor::run() noexcept {
  for (;;) {
    run_once();
#if defined(__arm__) || defined(__thumb__)
    __asm volatile("wfi");
#endif
  }
}
