#pragma once
#include "task.hpp"
#include <array>
#include <cstdint>
#include <coroutine>

extern "C" void SysTick_Handler();
std::uint32_t millis() noexcept;
/// Blockierend warten (SysTick muss laufen). Nur außerhalb von Tasks oder für sehr kurze
/// Zeiten verwenden; in Tasks `co_await Executor::sleep_for(ms)`.
void delay_ms(std::uint32_t ms) noexcept;

class Executor {
public:
    static constexpr std::size_t max_tasks = 8;
    bool spawn(Task&& task) noexcept;
    void run_once() noexcept;
    [[noreturn]] void run() noexcept;

    struct SleepAwaiter {
        std::uint32_t delay;
        std::uint32_t deadline{};
        bool await_ready() const noexcept { return delay == 0; }
        void await_suspend(std::coroutine_handle<> h) noexcept;
        void await_resume() const noexcept {}
    };
    static SleepAwaiter sleep_for(std::uint32_t ms) noexcept { return {ms, 0}; }

private:
    struct Slot { Task::handle_type handle{}; std::uint32_t wake_at{}; bool waiting{}; };
    std::array<Slot, max_tasks> slots_{};
    friend struct SleepAwaiter;
};
