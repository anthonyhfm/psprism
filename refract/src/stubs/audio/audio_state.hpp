#pragma once

#if !defined(__PSP__)

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <vector>

namespace audio_state {

struct Channel {
  bool reserved{};
  std::uint32_t sample_count{};
  std::uint32_t format{};
};

constexpr std::uint32_t stereo_format = 0U;
constexpr std::uint32_t mono_format = 0x10U;
constexpr std::uint32_t maximum_volume = 0x8000U;
constexpr std::uint32_t invalid_channel = 0x80260003U;
constexpr std::uint32_t invalid_format = 0x80260007U;
constexpr std::uint32_t not_reserved = 0x80260008U;
constexpr std::uint32_t invalid_volume = 0x8026000bU;

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
  if (format != stereo_format && format != mono_format)
    return static_cast<std::int32_t>(invalid_format);
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

inline std::int16_t read_sample(const std::uint8_t* input) {
  std::int16_t value{};
  std::memcpy(&value, input, sizeof(value));
  return value;
}

inline std::int16_t scale_sample(std::int16_t sample, std::uint32_t volume) {
  const auto scaled = static_cast<std::int64_t>(sample) * volume /
                      maximum_volume;
  return static_cast<std::int16_t>(std::clamp<std::int64_t>(
      scaled, std::numeric_limits<std::int16_t>::min(),
      std::numeric_limits<std::int16_t>::max()));
}

inline std::uint32_t output(psprecomp::State& state, std::uint32_t channel,
                            std::uint32_t left_volume,
                            std::uint32_t right_volume,
                            std::uint32_t buffer_address, bool blocking) {
  Channel selected;
  {
    std::lock_guard lock(mutex());
    if (channel >= channels().size()) return invalid_channel;
    if (!channels()[channel].reserved) return not_reserved;
    selected = channels()[channel];
  }
  if (left_volume > maximum_volume || right_volume > maximum_volume)
    return invalid_volume;
  const auto source_channels = selected.format == mono_format ? 1U : 2U;
  if (selected.format != mono_format && selected.format != stereo_format)
    return invalid_format;
  const auto byte_count = static_cast<std::size_t>(selected.sample_count) *
                          source_channels * sizeof(std::int16_t);
  const auto* input = psprecomp::mapped_address(state, buffer_address,
                                                 byte_count);
  if (input == nullptr) return 0xffffffffU;

  std::vector<std::int16_t> stereo(
      static_cast<std::size_t>(selected.sample_count) * 2U);
  for (std::size_t frame = 0; frame < selected.sample_count; ++frame) {
    const auto left = read_sample(input + frame * source_channels * 2U);
    const auto right = source_channels == 1U
                           ? left
                           : read_sample(input + frame * source_channels * 2U +
                                         sizeof(std::int16_t));
    stereo[frame * 2U] = scale_sample(left, left_volume);
    stereo[frame * 2U + 1U] = scale_sample(right, right_volume);
  }
  refract::host::submit_audio(stereo.data(), selected.sample_count);
  if (blocking && selected.sample_count != 0U) {
    constexpr std::uint64_t sample_rate = 44100U;
    refract::host::sleep_microseconds(static_cast<std::uint32_t>(
        std::max<std::uint64_t>(
            1U, selected.sample_count * 1000000ULL / sample_rate)));
  }
  return selected.sample_count;
}

inline void reset_for_tests() {
  std::lock_guard lock(mutex());
  channels().fill({});
}

} // namespace audio_state

#endif
