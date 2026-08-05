#pragma once

#if !defined(__PSP__)

#include <array>
#include <mutex>

namespace audio_state {

struct Channel {
  bool reserved{};
  std::uint32_t sample_count{};
  std::uint32_t format{};
};

inline std::mutex& mutex() {
  static std::mutex value;
  return value;
}

inline std::array<Channel, 8>& channels() {
  static std::array<Channel, 8> value{};
  return value;
}

inline int reserve(std::int32_t requested, std::uint32_t sample_count,
                   std::uint32_t format) {
  std::lock_guard lock(mutex());
  int selected = requested;
  if (selected < 0) {
    selected = -1;
    for (std::size_t index = 0; index < channels().size(); ++index) {
      if (!channels()[index].reserved) {
        selected = static_cast<int>(index);
        break;
      }
    }
  }
  if (selected < 0 || static_cast<std::size_t>(selected) >= channels().size() ||
      channels()[selected].reserved)
    return -1;
  channels()[selected] = Channel{true, sample_count, format};
  return selected;
}

inline bool release(std::uint32_t channel) {
  std::lock_guard lock(mutex());
  if (channel >= channels().size() || !channels()[channel].reserved)
    return false;
  channels()[channel] = {};
  return true;
}

inline bool update(std::uint32_t channel, std::uint32_t sample_count,
                   bool set_length, std::uint32_t format, bool set_format) {
  std::lock_guard lock(mutex());
  if (channel >= channels().size() || !channels()[channel].reserved)
    return false;
  if (set_length) channels()[channel].sample_count = sample_count;
  if (set_format) channels()[channel].format = format;
  return true;
}

inline std::uint32_t sample_count(std::uint32_t channel) {
  std::lock_guard lock(mutex());
  if (channel >= channels().size() || !channels()[channel].reserved) return 0U;
  return channels()[channel].sample_count;
}

inline std::uint32_t output(std::uint32_t channel, bool blocking) {
  const auto samples = sample_count(channel);
  if (blocking && samples != 0U) {
    constexpr std::uint64_t sample_rate = 44100U;
    host::sleep_microseconds(static_cast<std::uint32_t>(
        std::max<std::uint64_t>(1U, samples * 1000000ULL / sample_rate)));
  }
  return samples;
}

} // namespace audio_state

#endif
