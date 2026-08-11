#pragma once

#if !defined(__PSP__)

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "../../host/host.hpp"

namespace sas_state {

constexpr std::uint32_t invalid_grain = 0x80420001U;
constexpr std::uint32_t invalid_max_voices = 0x80420002U;
constexpr std::uint32_t invalid_output_mode = 0x80420003U;
constexpr std::uint32_t invalid_sample_rate = 0x80420004U;
constexpr std::uint32_t bad_address = 0x80420005U;
constexpr std::uint32_t invalid_voice = 0x80420010U;
constexpr std::uint32_t invalid_noise_frequency = 0x80420011U;
constexpr std::uint32_t invalid_pitch = 0x80420012U;
constexpr std::uint32_t invalid_adsr_mode = 0x80420013U;
constexpr std::uint32_t invalid_parameter = 0x80420014U;
constexpr std::uint32_t invalid_loop = 0x80420015U;
constexpr std::uint32_t voice_paused = 0x80420016U;
constexpr std::uint32_t invalid_volume = 0x80420018U;
constexpr std::uint32_t invalid_adsr = 0x80420019U;
constexpr std::uint32_t invalid_pcm_size = 0x8042001aU;
constexpr std::uint32_t invalid_effect_type = 0x80420020U;
constexpr std::uint32_t invalid_effect_feedback = 0x80420021U;
constexpr std::uint32_t invalid_effect_delay = 0x80420022U;
constexpr std::uint32_t invalid_effect_volume = 0x80420023U;
constexpr std::uint32_t not_initialized = 0x80420100U;

constexpr std::size_t core_size = 3616U;
constexpr std::uint32_t max_voices = 32U;
constexpr std::uint32_t max_volume = 0x1000U;

struct Voice {
  bool configured{};
  bool playing{};
  bool paused{};
  bool loop{};
  bool indefinite{};
  std::uint64_t remaining_samples{};
  std::uint32_t pitch{0x1000U};
  std::uint32_t envelope_height{};
  std::int32_t left_volume{static_cast<std::int32_t>(max_volume)};
  std::int32_t right_volume{static_cast<std::int32_t>(max_volume)};
  std::uint64_t position{};
  std::size_t loop_start{};
  std::vector<std::int16_t> samples;
};

struct Core {
  std::uint32_t grain{};
  std::uint32_t output_mode{};
  std::array<Voice, max_voices> voices{};
};

inline std::mutex& mutex() {
  static std::mutex value;
  return value;
}

inline std::unordered_map<std::uint32_t, Core>& cores() {
  static std::unordered_map<std::uint32_t, Core> value;
  return value;
}

inline std::uint32_t key(std::uint32_t address) {
  return psprecomp::canonical_address(address);
}

inline Core* find_unlocked(std::uint32_t address) {
  const auto found = cores().find(key(address));
  return found == cores().end() ? nullptr : &found->second;
}

inline std::int16_t clamp_sample(std::int64_t sample) {
  return static_cast<std::int16_t>(std::clamp<std::int64_t>(
      sample, std::numeric_limits<std::int16_t>::min(),
      std::numeric_limits<std::int16_t>::max()));
}

inline std::vector<std::int16_t> decode_vag(const std::uint8_t* source,
                                             std::size_t size) {
  std::vector<std::int16_t> decoded;
  if (source == nullptr || size < 16U) return decoded;
  std::size_t offset = 0U;
  if (size >= 0x30U && source[0] == 'V' && source[1] == 'A' &&
      source[2] == 'G' && source[3] == 'p')
    offset = 0x30U;
  decoded.reserve((size - offset) / 16U * 28U);
  constexpr std::int32_t coefficients[5][2] = {
      {0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};
  std::int32_t previous_1 = 0;
  std::int32_t previous_2 = 0;
  for (; offset + 16U <= size; offset += 16U) {
    const auto predictor = std::min<std::uint32_t>(source[offset] >> 4U, 4U);
    const auto shift = std::min<std::uint32_t>(source[offset] & 0xfU, 12U);
    const auto flags = source[offset + 1U];
    for (std::size_t index = 0; index < 28U; ++index) {
      const auto packed = source[offset + 2U + index / 2U];
      const auto nibble = static_cast<std::int8_t>(
          index % 2U == 0U ? packed << 4U : packed & 0xf0U) >> 4U;
      auto sample = (static_cast<std::int32_t>(nibble) * 4096) >> shift;
      sample += (previous_1 * coefficients[predictor][0] +
                 previous_2 * coefficients[predictor][1] + 32) >>
                6U;
      const auto clamped = clamp_sample(sample);
      decoded.push_back(clamped);
      previous_2 = previous_1;
      previous_1 = clamped;
    }
    if ((flags & 1U) != 0U) break;
  }
  return decoded;
}

inline std::uint32_t initialize(psprecomp::State& state,
                                std::uint32_t core_address,
                                std::uint32_t grain,
                                std::uint32_t voices,
                                std::uint32_t output_mode,
                                std::uint32_t sample_rate) {
  const auto canonical = key(core_address);
  if ((canonical & 0x3fU) != 0U ||
      psprecomp::mapped_address(state, canonical, core_size) == nullptr)
    return bad_address;
  if (voices == 0U || voices > max_voices) return invalid_max_voices;
  if (grain < 64U || grain > 2048U || (grain & 0x1fU) != 0U)
    return invalid_grain;
  if (output_mode > 1U) return invalid_output_mode;
  if (sample_rate != 44100U) return invalid_sample_rate;

  std::lock_guard lock(mutex());
  Core core;
  core.grain = grain;
  core.output_mode = output_mode;
  cores()[canonical] = core;
  return 0U;
}

template <typename Function>
inline std::uint32_t update_core(std::uint32_t core_address,
                                 Function&& function) {
  std::lock_guard lock(mutex());
  auto* core = find_unlocked(core_address);
  return core == nullptr ? not_initialized : function(*core);
}

template <typename Function>
inline std::uint32_t update_voice(std::uint32_t core_address,
                                  std::int32_t voice_index,
                                  Function&& function) {
  if (voice_index < 0 || voice_index >= static_cast<std::int32_t>(max_voices))
    return invalid_voice;
  return update_core(core_address, [&](Core& core) {
    return function(core.voices[static_cast<std::size_t>(voice_index)]);
  });
}

inline std::uint32_t mix(psprecomp::State& state, std::uint32_t core_address,
                         std::uint32_t output_address, bool preserve) {
  std::lock_guard lock(mutex());
  auto* core = find_unlocked(core_address);
  if (core == nullptr) return not_initialized;
  const auto grain_size = core->grain;
  const auto channels = core->output_mode == 0U ? 2U : 4U;
  const auto sample_total = static_cast<std::size_t>(grain_size) * channels;
  const auto bytes = sample_total * sizeof(std::int16_t);
  auto* output = psprecomp::mapped_address(state, output_address, bytes);
  if (output == nullptr) return invalid_parameter;
  std::vector<std::int64_t> mixed(sample_total);
  if (preserve) {
    for (std::size_t index = 0; index < sample_total; ++index) {
      std::int16_t sample{};
      std::memcpy(&sample, output + index * sizeof(sample), sizeof(sample));
      mixed[index] = sample;
    }
  }

  for (auto& voice : core->voices) {
    if (!voice.playing || voice.paused || voice.samples.empty()) continue;
    for (std::size_t frame = 0; frame < grain_size; ++frame) {
      auto sample_index = static_cast<std::size_t>(voice.position >> 12U);
      if (sample_index >= voice.samples.size()) {
        if (!voice.loop) {
          voice.playing = false;
          voice.envelope_height = 0U;
          voice.remaining_samples = 0U;
          break;
        }
        sample_index = std::min(voice.loop_start, voice.samples.size() - 1U);
        voice.position = static_cast<std::uint64_t>(sample_index) << 12U;
      }
      const auto sample = voice.samples[sample_index];
      mixed[frame * channels] +=
          static_cast<std::int32_t>(sample) * voice.left_volume /
          static_cast<std::int32_t>(max_volume);
      mixed[frame * channels + 1U] +=
          static_cast<std::int32_t>(sample) * voice.right_volume /
          static_cast<std::int32_t>(max_volume);
      voice.position += voice.pitch;
    }
    const auto position = static_cast<std::size_t>(voice.position >> 12U);
    voice.remaining_samples = position < voice.samples.size()
                                  ? voice.samples.size() - position
                                  : 0U;
  }
  for (std::size_t index = 0; index < sample_total; ++index) {
    const auto sample = clamp_sample(mixed[index]);
    std::memcpy(output + index * sizeof(sample), &sample, sizeof(sample));
  }
  return 0U;
}

inline std::uint32_t end_flags(std::uint32_t core_address) {
  return update_core(core_address, [](Core& core) {
    std::uint32_t result = 0U;
    for (std::size_t index = 0; index < core.voices.size(); ++index) {
      if (!core.voices[index].playing) result |= 1U << index;
    }
    return result;
  });
}

inline std::uint32_t pause_flags(std::uint32_t core_address) {
  return update_core(core_address, [](Core& core) {
    std::uint32_t result = 0U;
    for (std::size_t index = 0; index < core.voices.size(); ++index) {
      if (core.voices[index].paused) result |= 1U << index;
    }
    return result;
  });
}

inline std::uint32_t set_pause(std::uint32_t core_address,
                               std::uint32_t voice_mask, bool paused) {
  return update_core(core_address, [&](Core& core) {
    for (std::size_t index = 0; index < core.voices.size(); ++index) {
      if ((voice_mask & (1U << index)) != 0U)
        core.voices[index].paused = paused;
    }
    return 0U;
  });
}

inline std::uint32_t set_voice(std::uint32_t core_address,
                               std::int32_t voice_index,
                               psprecomp::State& state,
                               std::uint32_t source_address,
                               std::int32_t source_size, std::int32_t loop) {
  if (voice_index < 0 || voice_index >= static_cast<std::int32_t>(max_voices))
    return invalid_voice;
  if (source_size <= 0 ||
      (static_cast<std::uint32_t>(source_size) & 0xfU) != 0U)
    return invalid_parameter;
  if (loop != 0 && loop != 1) return invalid_loop;
  const auto* source = psprecomp::mapped_address(
      state, source_address, static_cast<std::size_t>(source_size));
  if (source == nullptr)
    return invalid_parameter;
  auto samples = decode_vag(source, static_cast<std::size_t>(source_size));
  if (samples.empty()) return invalid_parameter;
  return update_voice(core_address, voice_index, [&](Voice& voice) {
    voice = {};
    voice.configured = true;
    voice.loop = loop != 0;
    voice.samples = std::move(samples);
    voice.remaining_samples = voice.samples.size();
    return 0U;
  });
}

inline std::uint32_t set_voice_pcm(std::uint32_t core_address,
                                   std::int32_t voice_index,
                                   psprecomp::State& state,
                                   std::uint32_t source_address,
                                   std::int32_t sample_count,
                                   std::int32_t loop_start) {
  if (voice_index < 0 || voice_index >= static_cast<std::int32_t>(max_voices))
    return invalid_voice;
  if (sample_count <= 0 || sample_count > 0x10000) return invalid_pcm_size;
  if (loop_start >= sample_count) return invalid_loop;
  const auto bytes = static_cast<std::size_t>(sample_count) *
                     sizeof(std::int16_t);
  const auto* source = psprecomp::mapped_address(state, source_address, bytes);
  if (source == nullptr)
    return invalid_parameter;
  return update_voice(core_address, voice_index, [&](Voice& voice) {
    voice = {};
    voice.configured = true;
    voice.loop = loop_start >= 0;
    voice.loop_start = loop_start >= 0 ? static_cast<std::size_t>(loop_start)
                                       : 0U;
    voice.samples.resize(static_cast<std::size_t>(sample_count));
    std::memcpy(voice.samples.data(), source, bytes);
    voice.remaining_samples = static_cast<std::uint32_t>(sample_count);
    return 0U;
  });
}

inline std::uint32_t key_on(std::uint32_t core_address,
                            std::int32_t voice_index) {
  return update_voice(core_address, voice_index, [](Voice& voice) {
    if (voice.paused || voice.playing) return voice_paused;
    voice.position = 0U;
    voice.remaining_samples = voice.samples.size();
    voice.playing = true;
    voice.envelope_height = 0x40000000U;
    return 0U;
  });
}

inline std::uint32_t key_off(std::uint32_t core_address,
                             std::int32_t voice_index) {
  return update_voice(core_address, voice_index, [](Voice& voice) {
    if (voice.paused || !voice.playing) return voice_paused;
    voice.playing = false;
    voice.envelope_height = 0U;
    return 0U;
  });
}

inline std::uint32_t set_pitch(std::uint32_t core_address,
                               std::int32_t voice_index,
                               std::uint32_t pitch) {
  if (pitch > 0x4000U) return invalid_pitch;
  return update_voice(core_address, voice_index, [&](Voice& voice) {
    voice.pitch = pitch;
    return 0U;
  });
}

inline bool volume_valid(std::int32_t volume) {
  const auto wide = static_cast<std::int64_t>(volume);
  return wide >= -static_cast<std::int64_t>(max_volume) &&
         wide <= static_cast<std::int64_t>(max_volume);
}

inline std::uint32_t set_volume(std::uint32_t core_address,
                                std::int32_t voice_index,
                                std::int32_t left, std::int32_t right,
                                std::int32_t effect_left,
                                std::int32_t effect_right) {
  if (!volume_valid(left) || !volume_valid(right) ||
      !volume_valid(effect_left) || !volume_valid(effect_right))
    return invalid_volume;
  return update_voice(core_address, voice_index, [&](Voice& voice) {
    voice.left_volume = left;
    voice.right_volume = right;
    return 0U;
  });
}

inline std::uint32_t set_noise(std::uint32_t core_address,
                               std::int32_t voice_index,
                               std::int32_t frequency) {
  if (frequency < 0 || frequency >= 64) return invalid_noise_frequency;
  return update_voice(core_address, voice_index, [](Voice& voice) {
    voice.configured = true;
    voice.indefinite = true;
    return 0U;
  });
}

inline std::uint32_t envelope_height(std::uint32_t core_address,
                                     std::int32_t voice_index) {
  return update_voice(core_address, voice_index,
                      [](Voice& voice) { return voice.envelope_height; });
}

inline std::uint32_t all_envelope_heights(psprecomp::State& state,
                                          std::uint32_t core_address,
                                          std::uint32_t output_address) {
  auto* output = psprecomp::mapped_address(state, output_address,
                                           max_voices * sizeof(std::uint32_t));
  if (output == nullptr) return invalid_parameter;
  return update_core(core_address, [&](Core& core) {
    for (std::size_t index = 0; index < core.voices.size(); ++index) {
      const auto value = core.voices[index].envelope_height;
      output[index * 4U] = static_cast<std::uint8_t>(value);
      output[index * 4U + 1U] = static_cast<std::uint8_t>(value >> 8U);
      output[index * 4U + 2U] = static_cast<std::uint8_t>(value >> 16U);
      output[index * 4U + 3U] = static_cast<std::uint8_t>(value >> 24U);
    }
    return 0U;
  });
}

inline std::uint32_t set_output_mode(std::uint32_t core_address,
                                     std::uint32_t output_mode) {
  if (output_mode > 1U) return invalid_output_mode;
  return update_core(core_address, [&](Core& core) {
    core.output_mode = output_mode;
    return 0U;
  });
}

inline std::uint32_t output_mode(std::uint32_t core_address) {
  return update_core(core_address,
                     [](Core& core) { return core.output_mode; });
}

inline std::uint32_t set_grain(std::uint32_t core_address,
                               std::uint32_t grain) {
  if (grain < 64U || grain > 2048U || (grain & 0x1fU) != 0U)
    return invalid_grain;
  return update_core(core_address, [&](Core& core) {
    core.grain = grain;
    return 0U;
  });
}

inline std::uint32_t grain(std::uint32_t core_address) {
  return update_core(core_address, [](Core& core) { return core.grain; });
}

inline std::uint32_t validate_voice(std::uint32_t core_address,
                                    std::int32_t voice_index) {
  return update_voice(core_address, voice_index, [](Voice&) { return 0U; });
}

inline void reset_for_tests() {
  std::lock_guard lock(mutex());
  cores().clear();
}

} // namespace sas_state

#endif
