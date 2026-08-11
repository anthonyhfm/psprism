#pragma once

#include <cstdint>

namespace display_state {

constexpr std::uint64_t vblank_period_microseconds = 16683U;

constexpr std::uint64_t
microseconds_until_next_vblank(std::uint64_t elapsed_microseconds) {
  const auto position = elapsed_microseconds % vblank_period_microseconds;
  return position == 0U ? vblank_period_microseconds
                        : vblank_period_microseconds - position;
}

} // namespace display_state
