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
  std::uint32_t left_volume{};
  std::uint32_t right_volume{};
};

constexpr std::uint32_t stereo_format = 0U;
constexpr std::uint32_t mono_format = 0x10U;
constexpr std::uint32_t maximum_volume = 0x8000U;
constexpr std::uint32_t maximum_api_volume = 0xffffU;
constexpr std::uint32_t output2_maximum_volume = 0xfffffU;
constexpr std::uint32_t output2_channel = 8U;
constexpr std::uint32_t channel_busy = 0x80260002U;
constexpr std::uint32_t invalid_channel = 0x80260003U;
constexpr std::uint32_t invalid_format = 0x80260007U;
constexpr std::uint32_t not_reserved = 0x80260008U;
constexpr std::uint32_t invalid_volume = 0x8026000bU;
constexpr std::uint32_t already_reserved = 0x80268002U;
constexpr std::uint32_t invalid_size = 0x800201bcU;

inline std::mutex& mutex() {
  static std::mutex value;
  return value;
}

inline std::array<Channel, 8>& channels() {
  static std::array<Channel, 8> value{};
  return value;
}

inline Channel& output2() {
  static Channel value{};
  return value;
}

inline int reserve(std::int32_t requested, std::uint32_t sample_count,
                   std::uint32_t format) {
  if (format != stereo_format && format != mono_format)
    return static_cast<std::int32_t>(invalid_format);
  int selected = requested;
  {
    std::lock_guard lock(mutex());
    if (selected < 0) {
      selected = -1;
      for (std::size_t index = 0; index < channels().size(); ++index) {
        if (!channels()[index].reserved) {
          selected = static_cast<int>(index);
          break;
        }
      }
    }
    if (selected < 0 ||
        static_cast<std::size_t>(selected) >= channels().size() ||
        channels()[selected].reserved)
      return -1;
    channels()[selected] = Channel{true, sample_count, format};
  }
  refract::host::reset_audio_channel(static_cast<std::uint32_t>(selected));
  return selected;
}

inline bool release(std::uint32_t channel) {
  {
    std::lock_guard lock(mutex());
    if (channel >= channels().size() || !channels()[channel].reserved)
      return false;
    channels()[channel] = {};
  }
  refract::host::reset_audio_channel(channel);
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

inline bool valid_volume_argument(std::int32_t volume,
                                  bool allow_sentinel = true) {
  return (allow_sentinel && volume < 0) ||
         static_cast<std::uint32_t>(volume) <= maximum_api_volume;
}

inline std::uint32_t change_volume(std::uint32_t channel,
                                   std::int32_t left_volume,
                                   std::int32_t right_volume) {
  if (!valid_volume_argument(left_volume, false) ||
      !valid_volume_argument(right_volume, false))
    return invalid_volume;
  std::lock_guard lock(mutex());
  if (channel >= channels().size()) return invalid_channel;
  if (!channels()[channel].reserved) return not_reserved;
  channels()[channel].left_volume = static_cast<std::uint32_t>(left_volume);
  channels()[channel].right_volume = static_cast<std::uint32_t>(right_volume);
  return 0U;
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
                            std::int32_t left_volume,
                            std::int32_t right_volume,
                            std::uint32_t buffer_address, bool blocking,
                            bool reject_negative_volume = false) {
  if (!valid_volume_argument(left_volume, !reject_negative_volume) ||
      !valid_volume_argument(right_volume, !reject_negative_volume))
    return invalid_volume;
  Channel selected;
  {
    std::lock_guard lock(mutex());
    if (channel >= channels().size()) return invalid_channel;
    if (!channels()[channel].reserved) return not_reserved;
    auto& selected_channel = channels()[channel];
    if (left_volume >= 0)
      selected_channel.left_volume =
          static_cast<std::uint32_t>(left_volume);
    if (right_volume >= 0)
      selected_channel.right_volume =
          static_cast<std::uint32_t>(right_volume);
    selected = selected_channel;
  }
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
    stereo[frame * 2U] = scale_sample(left, selected.left_volume);
    stereo[frame * 2U + 1U] = scale_sample(right, selected.right_volume);
  }
  if (!refract::host::submit_audio(stereo.data(), selected.sample_count,
                                   channel, blocking))
    return channel_busy;
  return selected.sample_count;
}

inline std::uint32_t rest_length(std::uint32_t channel) {
  if (channel >= channels().size()) return invalid_channel;
  return refract::host::queued_audio_frames(channel);
}

inline std::uint32_t reserve_output2(std::uint32_t sample_count) {
  sample_count &= 0x7fffffffU;
  if (sample_count < 17U || sample_count > 4111U) return invalid_size;
  {
    std::lock_guard lock(mutex());
    if (output2().reserved) return already_reserved;
    output2() = Channel{true, sample_count, stereo_format, maximum_volume,
                        maximum_volume};
  }
  refract::host::reset_audio_channel(output2_channel);
  return 0U;
}

inline std::uint32_t change_output2_length(std::uint32_t sample_count) {
  std::lock_guard lock(mutex());
  if (!output2().reserved) return not_reserved;
  output2().sample_count = sample_count;
  return 0U;
}

inline std::uint32_t output2_rest_length() {
  std::uint32_t length{};
  {
    std::lock_guard lock(mutex());
    if (!output2().reserved) return not_reserved;
    length = output2().sample_count;
  }
  return std::min(length,
                  refract::host::queued_audio_frames(output2_channel));
}

inline std::uint32_t release_output2() {
  {
    std::lock_guard lock(mutex());
    if (!output2().reserved) return not_reserved;
    if (refract::host::queued_audio_frames(output2_channel) != 0U)
      return already_reserved;
    output2() = {};
  }
  refract::host::reset_audio_channel(output2_channel);
  return 0U;
}

inline std::uint32_t output_output2(psprecomp::State& state,
                                    std::uint32_t volume,
                                    std::uint32_t buffer_address,
                                    bool blocking) {
  Channel selected;
  {
    std::lock_guard lock(mutex());
    if (!output2().reserved) return not_reserved;
    selected = output2();
  }
  if (volume > output2_maximum_volume) return invalid_volume;
  const auto byte_count = static_cast<std::size_t>(selected.sample_count) * 2U *
                          sizeof(std::int16_t);
  const auto* input =
      psprecomp::mapped_address(state, buffer_address, byte_count);
  if (input == nullptr) return 0xffffffffU;
  std::vector<std::int16_t> stereo(
      static_cast<std::size_t>(selected.sample_count) * 2U);
  for (std::size_t sample = 0U; sample < stereo.size(); ++sample)
    stereo[sample] = read_sample(input + sample * sizeof(std::int16_t));
  if (volume != maximum_volume) {
    for (auto& sample : stereo) sample = scale_sample(sample, volume);
  }
  if (!refract::host::submit_audio(stereo.data(), selected.sample_count,
                                   output2_channel, blocking))
    return channel_busy;
  return selected.sample_count;
}

inline void reset_for_tests() {
  {
    std::lock_guard lock(mutex());
    channels().fill({});
    output2() = {};
  }
  for (std::uint32_t channel = 0U; channel <= output2_channel; ++channel)
    refract::host::reset_audio_channel(channel);
}

} // namespace audio_state

#endif
