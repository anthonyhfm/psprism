#pragma once

#include <cstdint>

namespace devctl_state {

struct DeviceCapacity {
  std::uint32_t maximum_clusters{};
  std::uint32_t free_clusters{};
  std::uint32_t maximum_sectors{};
  std::uint32_t sector_size{};
  std::uint32_t sectors_per_cluster{};
};

constexpr DeviceCapacity memory_stick_capacity() {
  constexpr std::uint64_t free_bytes = 1536ULL * 1024ULL * 1024ULL;
  constexpr std::uint32_t sector_size = 512U;
  constexpr std::uint32_t sectors_per_cluster = 64U;
  constexpr auto clusters = static_cast<std::uint32_t>(
      (free_bytes * 95ULL / 100ULL) /
      (sector_size * sectors_per_cluster));
  return {clusters, clusters, clusters, sector_size, sectors_per_cluster};
}

} // namespace devctl_state
