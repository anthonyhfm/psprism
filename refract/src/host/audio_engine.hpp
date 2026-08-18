#pragma once

#include "host.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>

namespace refract::host {

// Lock-free mixer shared by the platform audio backend and unit tests.
// Audio consumer reads lock-free with zero mutex locks using SPSC circular ring buffers.
class AudioEngine {
 public:
  static constexpr std::size_t channel_count = 9U;
  static constexpr std::size_t maximum_frames_per_channel = 131072U;

  enum class SubmitResult {
    submitted,
    busy,
    invalid,
    timeout,
  };

  AudioEngine();
  AudioEngine(const AudioEngine&) = delete;
  AudioEngine& operator=(const AudioEngine&) = delete;

  SubmitResult submit(const std::int16_t* interleaved_stereo,
                      std::uint32_t frame_count, std::uint32_t channel,
                      bool blocking,
                      std::chrono::microseconds timeout,
                      bool recover_on_timeout = false);
  std::uint32_t consume(std::int16_t* interleaved_stereo,
                        std::uint32_t frame_count) noexcept;
  [[nodiscard]] std::uint32_t queued_frames(std::uint32_t channel) const noexcept;
  [[nodiscard]] AudioTelemetry telemetry() const noexcept;
  [[nodiscard]] std::uint64_t clock_frames() const noexcept {
    return consumed_frames_.load(std::memory_order_relaxed);
  }
  void reset_channel(std::uint32_t channel);
  // A host-device reset discards queued data while preserving the monotonic
  // master clock and cumulative telemetry.
  void device_reset();
  void reset();

 private:
  struct Channel {
    alignas(64) std::atomic<std::size_t> write_index_{0};
    alignas(64) std::atomic<std::size_t> read_index_{0};
    std::array<std::int16_t, maximum_frames_per_channel * 2U> samples{};
    mutable std::mutex submit_mutex;
    std::condition_variable space_available;
  };

  std::unique_ptr<std::array<Channel, channel_count>> channels_;
  std::atomic<std::uint64_t> submitted_frames_{};
  std::atomic<std::uint64_t> consumed_frames_{};
  std::atomic<std::uint64_t> queued_frames_{};
  std::atomic<std::uint64_t> peak_queued_frames_{};
  std::atomic<std::uint64_t> callback_count_{};
  std::atomic<std::uint64_t> underrun_callbacks_{};
  std::atomic<std::uint64_t> underrun_frames_{};
  std::atomic<std::uint64_t> overrun_submissions_{};
  std::atomic<std::uint64_t> dropped_frames_{};
  std::atomic<std::uint64_t> rejected_submissions_{};
  std::atomic<std::uint64_t> callback_lock_contentions_{};
  std::atomic<std::uint64_t> device_resets_{};
};

static_assert(noexcept(std::declval<AudioEngine&>().consume(nullptr, 0U)));

} // namespace refract::host
