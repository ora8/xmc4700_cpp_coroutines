#include "coroutine_pool.hpp"
#include <array>
#include <cstdint>

namespace {
constexpr std::size_t block_size = 256;
constexpr std::size_t block_count = 8;
struct alignas(16) Block { std::array<std::byte, block_size> bytes{}; };
std::array<Block, block_count> pool{};
std::array<bool, block_count> used{};
}

namespace coro_pool {
void* allocate(std::size_t size, std::size_t alignment) noexcept {
    if (size > block_size || alignment > alignof(Block)) return nullptr;
    for (std::size_t i = 0; i < block_count; ++i) {
        if (!used[i]) { used[i] = true; return pool[i].bytes.data(); }
    }
    return nullptr;
}
void deallocate(void* ptr) noexcept {
    if (!ptr) return;
    for (std::size_t i = 0; i < block_count; ++i)
        if (ptr == pool[i].bytes.data()) { used[i] = false; return; }
}
std::size_t free_blocks() noexcept {
    std::size_t n = 0;
    for (bool b : used) if (!b) ++n;
    return n;
}
}
