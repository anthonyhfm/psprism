#pragma once

#include <cstdint>

namespace psprism::host {

std::uint64_t monotonic_microseconds();
std::uint64_t unix_seconds();
void sleep_microseconds(std::uint32_t duration);

} // namespace psprism::host
