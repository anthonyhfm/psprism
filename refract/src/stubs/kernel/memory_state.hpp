#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace memory_state {

template <typename Blocks>
std::uint32_t maximum_free_size(std::uint32_t heap_cursor,
                                std::uint32_t stack_cursor,
                                const Blocks& free_blocks) {
  auto largest = stack_cursor > heap_cursor ? stack_cursor - heap_cursor : 0U;
  for (const auto& block : free_blocks)
    largest = std::max(largest, block.size);
  return largest;
}

template <typename Blocks>
std::uint32_t total_free_size(std::uint32_t heap_cursor,
                              std::uint32_t stack_cursor,
                              const Blocks& free_blocks) {
  std::uint64_t total =
      stack_cursor > heap_cursor ? stack_cursor - heap_cursor : 0U;
  for (const auto& block : free_blocks)
    total += block.size;
  return static_cast<std::uint32_t>(
      std::min<std::uint64_t>(total,
                              std::numeric_limits<std::uint32_t>::max()));
}

}  // namespace memory_state
