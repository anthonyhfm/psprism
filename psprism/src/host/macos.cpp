#include "host.hpp"

#if !defined(__APPLE__)
#error "psprism currently ships only a macOS host backend"
#endif

#include <chrono>
#include <thread>

namespace psprism::host {

std::uint64_t monotonic_microseconds() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

std::uint64_t unix_seconds() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

void sleep_microseconds(std::uint32_t duration) {
  std::this_thread::sleep_for(std::chrono::microseconds(duration));
}

} // namespace psprism::host
