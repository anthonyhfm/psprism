#pragma once

#if !defined(__PSP__)

#include <cstdint>
#include <mutex>

namespace ctrl_state {

constexpr std::uint32_t invalid_mode = 0x80000107U;
constexpr std::uint32_t invalid_value = 0x800001feU;

struct Settings {
  std::uint32_t sampling_cycle{};
  std::uint32_t sampling_mode{};
  std::int32_t idle_reset{-1};
  std::int32_t idle_back{-1};
};

inline std::mutex& mutex() {
  static std::mutex value;
  return value;
}

inline Settings& settings() {
  static Settings value;
  return value;
}

inline std::uint32_t set_sampling_cycle(std::uint32_t cycle) {
  if ((cycle != 0U && cycle < 5555U) || cycle > 20000U)
    return invalid_value;
  std::lock_guard lock(mutex());
  const auto previous = settings().sampling_cycle;
  settings().sampling_cycle = cycle;
  return previous;
}

inline std::uint32_t set_sampling_mode(std::uint32_t mode) {
  if (mode > 1U) return invalid_mode;
  std::lock_guard lock(mutex());
  const auto previous = settings().sampling_mode;
  settings().sampling_mode = mode;
  return previous;
}

inline std::uint32_t set_idle_cancel_threshold(std::int32_t idle_reset,
                                               std::int32_t idle_back) {
  if (idle_reset < -1 || idle_reset > 128 || idle_back < -1 ||
      idle_back > 128)
    return invalid_value;
  std::lock_guard lock(mutex());
  settings().idle_reset = idle_reset;
  settings().idle_back = idle_back;
  return 0U;
}

inline void reset_for_tests() {
  std::lock_guard lock(mutex());
  settings() = {};
}

} // namespace ctrl_state

#endif
