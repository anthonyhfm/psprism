#pragma once

#include <atomic>
#include <cstdint>

namespace refract::ge {

struct CacheMetrics {
  std::uint64_t texture_hits{};
  std::uint64_t texture_misses{};
  std::uint64_t texture_upload_bytes{};
  std::uint64_t pipeline_hits{};
  std::uint64_t pipeline_misses{};
  std::uint64_t vertex_buffer_reuses{};
  std::uint64_t vertex_buffer_allocations{};
  std::uint64_t vertex_upload_bytes{};

  friend bool operator==(const CacheMetrics&, const CacheMetrics&) = default;
};

class CacheMetricsAccumulator {
public:
  void record_texture(bool hit, std::uint64_t upload_bytes = 0U) {
    (hit ? texture_hits_ : texture_misses_).fetch_add(
        1U, std::memory_order_relaxed);
    if (!hit)
      texture_upload_bytes_.fetch_add(upload_bytes, std::memory_order_relaxed);
  }

  void record_pipeline(bool hit) {
    (hit ? pipeline_hits_ : pipeline_misses_).fetch_add(
        1U, std::memory_order_relaxed);
  }

  void record_vertex_buffer(bool reused, std::uint64_t upload_bytes) {
    (reused ? vertex_buffer_reuses_ : vertex_buffer_allocations_)
        .fetch_add(1U, std::memory_order_relaxed);
    vertex_upload_bytes_.fetch_add(upload_bytes, std::memory_order_relaxed);
  }

  CacheMetrics snapshot() const {
    return {texture_hits_.load(std::memory_order_relaxed),
            texture_misses_.load(std::memory_order_relaxed),
            texture_upload_bytes_.load(std::memory_order_relaxed),
            pipeline_hits_.load(std::memory_order_relaxed),
            pipeline_misses_.load(std::memory_order_relaxed),
            vertex_buffer_reuses_.load(std::memory_order_relaxed),
            vertex_buffer_allocations_.load(std::memory_order_relaxed),
            vertex_upload_bytes_.load(std::memory_order_relaxed)};
  }

  void reset() {
    texture_hits_.store(0U, std::memory_order_relaxed);
    texture_misses_.store(0U, std::memory_order_relaxed);
    texture_upload_bytes_.store(0U, std::memory_order_relaxed);
    pipeline_hits_.store(0U, std::memory_order_relaxed);
    pipeline_misses_.store(0U, std::memory_order_relaxed);
    vertex_buffer_reuses_.store(0U, std::memory_order_relaxed);
    vertex_buffer_allocations_.store(0U, std::memory_order_relaxed);
    vertex_upload_bytes_.store(0U, std::memory_order_relaxed);
  }

private:
  std::atomic<std::uint64_t> texture_hits_{};
  std::atomic<std::uint64_t> texture_misses_{};
  std::atomic<std::uint64_t> texture_upload_bytes_{};
  std::atomic<std::uint64_t> pipeline_hits_{};
  std::atomic<std::uint64_t> pipeline_misses_{};
  std::atomic<std::uint64_t> vertex_buffer_reuses_{};
  std::atomic<std::uint64_t> vertex_buffer_allocations_{};
  std::atomic<std::uint64_t> vertex_upload_bytes_{};
};

} // namespace refract::ge
