#pragma once
#include "coroutine_pool.hpp"
#include <coroutine>
#include <cstddef>
#include <new>

class Executor;

class Task {
public:
    struct promise_type {
        static void* operator new(std::size_t n) noexcept {
            return coro_pool::allocate(n, alignof(std::max_align_t));
        }
        static void operator delete(void* p, std::size_t) noexcept { coro_pool::deallocate(p); }
        static void operator delete(void* p) noexcept { coro_pool::deallocate(p); }
        static Task get_return_object_on_allocation_failure() noexcept { return {}; }
        Task get_return_object() noexcept {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() const noexcept { return {}; }
        std::suspend_always final_suspend() const noexcept { return {}; }
        void return_void() const noexcept {}
        void unhandled_exception() const noexcept { for (;;) {} }
    };

    using handle_type = std::coroutine_handle<promise_type>;
    Task() noexcept = default;
    explicit Task(handle_type h) noexcept : handle_(h) {}
    Task(Task&& other) noexcept : handle_(other.handle_) { other.handle_ = {}; }
    Task& operator=(Task&& other) noexcept {
        if (this != &other) { if (handle_) handle_.destroy(); handle_ = other.handle_; other.handle_ = {}; }
        return *this;
    }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() { if (handle_) handle_.destroy(); }
    handle_type release() noexcept { auto h = handle_; handle_ = {}; return h; }
    explicit operator bool() const noexcept { return static_cast<bool>(handle_); }
private:
    handle_type handle_{};
};
