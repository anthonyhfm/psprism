#pragma once

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace refract::host {

// Fixed-storage mixer shared by the platform audio backend and unit tests.
// Producers may block, but the real-time consumer only ever uses try_lock and
// never allocates memory.
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

  AudioEngine() = default;
  AudioEngine(const AudioEngine&) = delete;
  AudioEngine& operator=(const AudioEngine&) = delete;

  SubmitResult submit(const std::int16_t* interleaved_stereo,
                      std::uint32_t frame_count, std::uint32_t channel,
                      bool blocking,
                      std::chrono::microseconds timeout);
  std::uint32_t consume(std::int16_t* interleaved_stereo,
                        std::uint32_t frame_count) noexcept;
  [[nodiscard]] std::uint32_t queued_frames(std::uint32_t channel) const;
  void reset_channel(std::uint32_t channel);
  void reset();

 private:
  struct Channel {
    mutable std::mutex mutex;
    std::condition_variable space_available;
    std::array<std::int16_t, maximum_frames_per_channel * 2U> samples{};
    std::size_t read_frame{};
    std::size_t queued_frames{};
  };

  std::array<Channel, channel_count> channels_{};
};

} // namespace refract::host
