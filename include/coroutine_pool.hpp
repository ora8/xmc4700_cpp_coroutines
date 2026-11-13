#pragma once
#include <cstddef>
#include <cstdint>

namespace coro_pool {
void* allocate(std::size_t size, std::size_t alignment) noexcept;
void deallocate(void* ptr) noexcept;
std::size_t free_blocks() noexcept;
}
