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
constexpr std::uint32_t max_envelope_height = 0x40000000U;

enum class EnvelopePhase {
  off,
  attack,
  decay,
  sustain,
  release,
};

enum class EnvelopeCurve : std::uint32_t {
  linear_increase = 0U,
  linear_decrease = 1U,
  linear_bent = 2U,
  exponent_decrease = 3U,
  exponent_increase = 4U,
  direct = 5U,
};

enum class VoiceType {
  off,
  vag,
  pcm,
  noise,
};

struct Voice {
  bool configured{};
  bool playing{};
  bool on{};
  bool paused{};
  bool loop{};
  bool requested_loop{};
  bool indefinite{};
  VoiceType type{VoiceType::off};
  std::uint32_t source_address{};
  std::int32_t source_size{};
  std::uint64_t remaining_samples{};
  std::uint32_t pitch{0x1000U};
  std::uint32_t envelope_height{};
  std::int32_t left_volume{static_cast<std::int32_t>(max_volume)};
  std::int32_t right_volume{static_cast<std::int32_t>(max_volume)};
  std::int32_t effect_left_volume{};
  std::int32_t effect_right_volume{};
  std::uint64_t position{};
  std::size_t loop_start{};
  bool envelope_configured{};
  EnvelopePhase envelope_phase{EnvelopePhase::off};
  std::uint32_t attack_rate{};
  std::uint32_t decay_rate{};
  std::uint32_t sustain_rate{};
  std::uint32_t release_rate{};
  std::uint32_t sustain_level{max_envelope_height};
  EnvelopeCurve attack_curve{EnvelopeCurve::linear_increase};
  EnvelopeCurve decay_curve{EnvelopeCurve::linear_decrease};
  EnvelopeCurve sustain_curve{EnvelopeCurve::linear_decrease};
  EnvelopeCurve release_curve{EnvelopeCurve::linear_decrease};
  std::vector<std::int16_t> samples;
};

struct Core {
  std::uint32_t grain{};
  std::uint32_t output_mode{};
  std::array<Voice, max_voices> voices{};
  std::array<std::int64_t, 2048U * 4U> mix_buffer{};
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

inline std::uint32_t walk_envelope_curve(std::uint32_t height,
                                         EnvelopeCurve curve,
                                         std::uint32_t rate) {
  const auto subtract = [](std::uint32_t value, std::uint32_t amount) {
    return amount >= value ? 0U : value - amount;
  };
  switch (curve) {
  case EnvelopeCurve::linear_increase:
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(
        max_envelope_height, static_cast<std::uint64_t>(height) + rate));
  case EnvelopeCurve::linear_decrease:
    return subtract(height, rate);
  case EnvelopeCurve::linear_bent: {
    const auto adjusted_rate =
        height <= max_envelope_height * 3U / 4U ? rate : rate / 4U;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(
        max_envelope_height,
        static_cast<std::uint64_t>(height) + adjusted_rate));
  }
  case EnvelopeCurve::exponent_decrease: {
    const auto distance =
        static_cast<std::uint64_t>(max_envelope_height - height);
    const auto delta = distance * rate >> 32U;
    const auto reduction = (static_cast<std::uint64_t>(rate) + 3U) / 4U;
    const auto next = static_cast<std::int64_t>(height) +
                      static_cast<std::int64_t>(delta) -
                      static_cast<std::int64_t>(reduction);
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(
        next, 0, max_envelope_height));
  }
  case EnvelopeCurve::exponent_increase: {
    const auto distance = max_envelope_height - height;
    const auto delta =
        (static_cast<std::uint64_t>(distance) * rate >> 32U) + 0x4000U;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(
        max_envelope_height, static_cast<std::uint64_t>(height) + delta));
  }
  case EnvelopeCurve::direct:
    return std::min(rate, max_envelope_height);
  }
  return height;
}

inline void advance_envelope(Voice& voice) {
  if (!voice.envelope_configured) {
    voice.envelope_height = max_envelope_height;
    return;
  }
  switch (voice.envelope_phase) {
  case EnvelopePhase::attack:
    voice.envelope_height = walk_envelope_curve(
        voice.envelope_height, voice.attack_curve, voice.attack_rate);
    if (voice.envelope_height >= max_envelope_height)
      voice.envelope_phase = EnvelopePhase::decay;
    break;
  case EnvelopePhase::decay:
    voice.envelope_height = walk_envelope_curve(
        voice.envelope_height, voice.decay_curve, voice.decay_rate);
    if (voice.envelope_height <= voice.sustain_level) {
      voice.envelope_height = voice.sustain_level;
      voice.envelope_phase = EnvelopePhase::sustain;
    }
    break;
  case EnvelopePhase::sustain:
    voice.envelope_height = walk_envelope_curve(
        voice.envelope_height, voice.sustain_curve, voice.sustain_rate);
    break;
  case EnvelopePhase::release:
    voice.envelope_height = walk_envelope_curve(
        voice.envelope_height, voice.release_curve, voice.release_rate);
    if (voice.envelope_height == 0U) {
      voice.envelope_phase = EnvelopePhase::off;
      voice.playing = false;
      voice.on = false;
      voice.remaining_samples = 0U;
    }
    break;
  case EnvelopePhase::off:
    voice.envelope_height = 0U;
    break;
  }
}

struct DecodedVag {
  std::vector<std::int16_t> samples;
  std::size_t loop_start{};
  bool has_loop_start{};
  bool has_loop_end{};
};

inline DecodedVag decode_vag_details(const std::uint8_t* source,
                                     std::size_t size) {
  DecodedVag result;
  if (source == nullptr || size < 16U) return result;
  std::size_t offset = 0U;
  if (size >= 0x30U && source[0] == 'V' && source[1] == 'A' &&
      source[2] == 'G' && source[3] == 'p')
    offset = 0x30U;
  result.samples.reserve((size - offset) / 16U * 28U);
  constexpr std::int32_t coefficients[16][2] = {
      {0, 0},    {60, 0},    {115, -52}, {98, -55},  {122, -60},
      {0, 0},    {0, 0},     {52, 0},    {55, -2},   {60, -125},
      {0, 0},    {0, -91},   {0, 0},     {2, -216},  {125, -6},
      {0, -151},
  };
  std::int32_t previous_1 = 0;
  std::int32_t previous_2 = 0;
  for (; offset + 16U <= size; offset += 16U) {
    const auto predictor = static_cast<std::uint32_t>(source[offset] >> 4U);
    const auto shift = static_cast<std::uint32_t>(source[offset] & 0xfU);
    const auto flags = source[offset + 1U];
    if (flags == 7U) break;
    if (flags == 6U) {
      result.loop_start = result.samples.size();
      result.has_loop_start = true;
    }
    for (std::size_t index = 0; index < 28U; ++index) {
      const auto packed = source[offset + 2U + index / 2U];
      const auto nibble = static_cast<std::int8_t>(
          index % 2U == 0U ? packed << 4U : packed & 0xf0U) >> 4U;
      auto sample = (static_cast<std::int32_t>(nibble) * 4096) >> shift;
      sample += (previous_1 * coefficients[predictor][0] +
                 previous_2 * coefficients[predictor][1] + 32) >>
                6U;
      const auto clamped = clamp_sample(sample);
      result.samples.push_back(clamped);
      previous_2 = previous_1;
      previous_1 = clamped;
    }
    if (flags == 3U) {
      result.has_loop_end = true;
      break;
    }
    if ((flags & 1U) != 0U) break;
  }
  return result;
}

inline std::vector<std::int16_t> decode_vag(const std::uint8_t* source,
                                            std::size_t size) {
  return decode_vag_details(source, size).samples;
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
                         std::uint32_t output_address, bool preserve,
                         std::int32_t preserve_left_volume =
                             static_cast<std::int32_t>(max_volume),
                         std::int32_t preserve_right_volume =
                             static_cast<std::int32_t>(max_volume)) {
  std::lock_guard lock(mutex());
  auto* core = find_unlocked(core_address);
  if (core == nullptr) return not_initialized;
  const auto grain_size = core->grain;
  const auto channels = core->output_mode == 0U ? 2U : 4U;
  const auto sample_total = static_cast<std::size_t>(grain_size) * channels;
  const auto bytes = sample_total * sizeof(std::int16_t);
  auto* output = psprecomp::mapped_address(state, output_address, bytes);
  if (output == nullptr) return invalid_parameter;
  auto* mixed = core->mix_buffer.data();
  std::fill_n(mixed, sample_total, 0);

  const bool is_aligned =
      (reinterpret_cast<std::uintptr_t>(output) % alignof(std::int16_t)) == 0;
  auto* output_samples = reinterpret_cast<std::int16_t*>(output);

  if (preserve) {
    for (std::size_t frame = 0; frame < grain_size; ++frame) {
      for (std::size_t side = 0; side < channels; ++side) {
        const auto output_index = channels == 4U
                                      ? side * grain_size + frame
                                      : frame * channels + side;
        std::int16_t sample{};
        if (is_aligned) {
          sample = output_samples[output_index];
        } else {
          std::memcpy(&sample, output + output_index * sizeof(sample),
                      sizeof(sample));
        }
        const auto volume = (side & 1U) == 0U ? preserve_left_volume
                                              : preserve_right_volume;
        mixed[frame * channels + side] =
            (static_cast<std::int32_t>(sample) * volume) >> 12;
      }
    }
  }

  for (auto& voice : core->voices) {
    if (!voice.playing || voice.paused || voice.samples.empty()) continue;
    for (std::size_t frame = 0; frame < grain_size; ++frame) {
      auto sample_index = static_cast<std::size_t>(voice.position >> 12U);
      if (sample_index >= voice.samples.size()) {
        if (!voice.loop) {
          voice.playing = false;
          voice.on = false;
          voice.envelope_height = 0U;
          voice.remaining_samples = 0U;
          break;
        }
        sample_index = std::min(voice.loop_start, voice.samples.size() - 1U);
        voice.position = static_cast<std::uint64_t>(sample_index) << 12U;
      }
      const auto next_index = sample_index + 1U < voice.samples.size()
                                  ? sample_index + 1U
                                  : (voice.loop ? std::min(
                                                     voice.loop_start,
                                                     voice.samples.size() - 1U)
                                                : sample_index);
      const auto fraction = static_cast<std::uint32_t>(voice.position & 0xfffU);
      const auto first_sample =
          static_cast<std::int32_t>(voice.samples[sample_index]);
      const auto second_sample =
          static_cast<std::int32_t>(voice.samples[next_index]);
      const auto interpolated =
          first_sample + ((second_sample - first_sample) *
                          static_cast<std::int32_t>(fraction) >>
                          12U);
      advance_envelope(voice);
      const auto sample = static_cast<std::int32_t>(
          (static_cast<std::int64_t>(interpolated) * voice.envelope_height) >> 30U);
      mixed[frame * channels] +=
          (static_cast<std::int32_t>(sample) * voice.left_volume) >> 12;
      mixed[frame * channels + 1U] +=
          (static_cast<std::int32_t>(sample) * voice.right_volume) >> 12;
      if (channels == 4U) {
        mixed[frame * channels + 2U] +=
            (sample * voice.effect_left_volume) >> 12;
        mixed[frame * channels + 3U] +=
            (sample * voice.effect_right_volume) >> 12;
      }
      voice.position += voice.pitch;
    }
    const auto position = static_cast<std::size_t>(voice.position >> 12U);
    voice.remaining_samples = position < voice.samples.size()
                                  ? voice.samples.size() - position
                                  : 0U;
  }

  if (is_aligned) {
    for (std::size_t frame = 0; frame < grain_size; ++frame) {
      for (std::size_t side = 0; side < channels; ++side) {
        const auto output_index = channels == 4U
                                      ? side * grain_size + frame
                                      : frame * channels + side;
        output_samples[output_index] =
            clamp_sample(mixed[frame * channels + side]);
      }
    }
  } else {
    for (std::size_t frame = 0; frame < grain_size; ++frame) {
      for (std::size_t side = 0; side < channels; ++side) {
        const auto sample = clamp_sample(mixed[frame * channels + side]);
        const auto output_index = channels == 4U
                                      ? side * grain_size + frame
                                      : frame * channels + side;
        std::memcpy(output + output_index * sizeof(sample), &sample,
                    sizeof(sample));
      }
    }
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
  auto decoded = decode_vag_details(source,
                                    static_cast<std::size_t>(source_size));
  if (decoded.samples.empty()) return invalid_parameter;
  return update_voice(core_address, voice_index, [&](Voice& voice) {
    const auto reset = voice.type != VoiceType::vag ||
                       voice.source_address != source_address ||
                       voice.source_size != source_size ||
                       voice.requested_loop != (loop != 0);
    voice.configured = true;
    voice.type = VoiceType::vag;
    voice.source_address = source_address;
    voice.source_size = source_size;
    voice.requested_loop = loop != 0;
    voice.loop = loop != 0 && decoded.has_loop_end;
    voice.loop_start = decoded.has_loop_start ? decoded.loop_start : 0U;
    if (reset) {
      voice.samples = std::move(decoded.samples);
      voice.position = 0U;
      voice.remaining_samples = voice.samples.size();
    }
    if (voice.on) voice.playing = true;
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
    voice.configured = true;
    voice.type = VoiceType::pcm;
    voice.source_address = source_address;
    voice.source_size = sample_count;
    voice.requested_loop = loop_start >= 0;
    voice.loop = loop_start >= 0;
    voice.loop_start = loop_start >= 0 ? static_cast<std::size_t>(loop_start)
                                       : 0U;
    voice.samples.resize(static_cast<std::size_t>(sample_count));
    std::memcpy(voice.samples.data(), source, bytes);
    voice.position = 0U;
    voice.remaining_samples = static_cast<std::uint32_t>(sample_count);
    voice.playing = true;
    return 0U;
  });
}

inline std::uint32_t key_on(std::uint32_t core_address,
                            std::int32_t voice_index) {
  return update_voice(core_address, voice_index, [](Voice& voice) {
    if (voice.paused || voice.on) return voice_paused;
    voice.position = 0U;
    voice.remaining_samples = voice.samples.size();
    voice.playing = true;
    voice.on = true;
    voice.envelope_height =
        voice.envelope_configured ? 0U : max_envelope_height;
    voice.envelope_phase = voice.envelope_configured
                               ? EnvelopePhase::attack
                               : EnvelopePhase::sustain;
    return 0U;
  });
}

inline std::uint32_t key_off(std::uint32_t core_address,
                             std::int32_t voice_index) {
  return update_voice(core_address, voice_index, [](Voice& voice) {
    if (voice.paused || !voice.on) return voice_paused;
    voice.on = false;
    if (voice.envelope_configured && voice.release_rate != 0U) {
      voice.envelope_phase = EnvelopePhase::release;
    } else {
      voice.playing = false;
      voice.envelope_height = 0U;
      voice.envelope_phase = EnvelopePhase::off;
    }
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
    voice.effect_left_volume = effect_left;
    voice.effect_right_volume = effect_right;
    return 0U;
  });
}

inline std::uint32_t set_adsr_rates(std::uint32_t core_address,
                                    std::int32_t voice_index,
                                    std::uint32_t mask,
                                    const std::array<std::uint32_t, 4>& rates) {
  return update_voice(core_address, voice_index, [&](Voice& voice) {
    if ((mask & 1U) != 0U) voice.attack_rate = rates[0];
    if ((mask & 2U) != 0U) voice.decay_rate = rates[1];
    if ((mask & 4U) != 0U) voice.sustain_rate = rates[2];
    if ((mask & 8U) != 0U) voice.release_rate = rates[3];
    voice.envelope_configured = true;
    return 0U;
  });
}

inline std::uint32_t set_adsr_curves(
    std::uint32_t core_address, std::int32_t voice_index, std::uint32_t mask,
    const std::array<std::uint32_t, 4>& curves) {
  return update_voice(core_address, voice_index, [&](Voice& voice) {
    if ((mask & 1U) != 0U)
      voice.attack_curve = static_cast<EnvelopeCurve>(curves[0]);
    if ((mask & 2U) != 0U)
      voice.decay_curve = static_cast<EnvelopeCurve>(curves[1]);
    if ((mask & 4U) != 0U)
      voice.sustain_curve = static_cast<EnvelopeCurve>(curves[2]);
    if ((mask & 8U) != 0U)
      voice.release_curve = static_cast<EnvelopeCurve>(curves[3]);
    voice.envelope_configured = true;
    return 0U;
  });
}

inline std::uint32_t simple_rate(std::uint32_t value) {
  const auto rate_index = value & 0x7fU;
  if (rate_index == 0x7fU) return 0U;
  const auto numerator = static_cast<std::uint64_t>(7U - (rate_index & 3U))
                         << 26U;
  return static_cast<std::uint32_t>(
      std::max<std::uint64_t>(1U, numerator >> (rate_index >> 2U)));
}

inline std::uint32_t exponent_rate(std::uint32_t value) {
  const auto rate_index = value & 0x7fU;
  if (rate_index == 0x7fU) return 0U;
  const auto numerator = static_cast<std::uint64_t>(7U - (rate_index & 3U))
                         << 24U;
  return static_cast<std::uint32_t>(
      std::max<std::uint64_t>(1U, numerator >> (rate_index >> 2U)));
}

inline std::uint32_t set_simple_adsr(std::uint32_t core_address,
                                     std::int32_t voice_index,
                                     std::uint32_t env1,
                                     std::uint32_t env2) {
  return update_voice(core_address, voice_index, [&](Voice& voice) {
    voice.attack_rate = simple_rate(env1 >> 8U);
    voice.attack_curve = (env1 & 0x8000U) == 0U
                             ? EnvelopeCurve::linear_increase
                             : EnvelopeCurve::linear_bent;
    const auto decay_index = (env1 >> 4U) & 0xfU;
    voice.decay_rate = decay_index == 0U
                           ? 0x7fffffffU
                           : 0x80000000U >> decay_index;
    voice.decay_curve = EnvelopeCurve::exponent_decrease;
    voice.sustain_level = ((env1 & 0xfU) + 1U) << 26U;
    const auto sustain_curve = (env2 >> 14U) & 3U;
    voice.sustain_curve = static_cast<EnvelopeCurve>(sustain_curve);
    voice.sustain_rate =
        sustain_curve ==
                static_cast<std::uint32_t>(EnvelopeCurve::exponent_decrease)
            ? exponent_rate(env2 >> 6U)
            : simple_rate(env2 >> 6U);
    const auto release_index = env2 & 0x1fU;
    voice.release_curve = (env2 & 0x20U) == 0U
                              ? EnvelopeCurve::linear_decrease
                              : EnvelopeCurve::exponent_decrease;
    if (release_index == 31U) {
      voice.release_rate = 0U;
    } else if (voice.release_curve == EnvelopeCurve::linear_decrease) {
      voice.release_rate = release_index == 30U
                               ? max_envelope_height
                               : release_index == 29U
                                     ? 1U
                                     : 0x10000000U >> release_index;
    } else {
      voice.release_rate = release_index == 0U
                               ? 0x7fffffffU
                               : 0x80000000U >> release_index;
    }
    voice.envelope_configured = true;
    return 0U;
  });
}

inline std::uint32_t set_sustain_level(std::uint32_t core_address,
                                       std::int32_t voice_index,
                                       std::uint32_t level) {
  return update_voice(core_address, voice_index, [&](Voice& voice) {
    voice.sustain_level = level;
    voice.envelope_configured = true;
    return 0U;
  });
}

inline std::uint32_t set_noise(std::uint32_t core_address,
                               std::int32_t voice_index,
                               std::int32_t frequency) {
  if (frequency < 0 || frequency >= 64) return invalid_noise_frequency;
  return update_voice(core_address, voice_index, [](Voice& voice) {
    voice.configured = true;
    voice.type = VoiceType::noise;
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
