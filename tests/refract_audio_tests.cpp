#include <psprecomp/runtime.hpp>

#include "host/host.hpp"
#include "stubs/audio/audio_state.hpp"
#include "stubs/audio/sas_state.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) return __LINE__;                                         \
  } while (false)

namespace {

std::vector<std::int16_t> submitted_audio;
std::uint32_t submitted_channel{};
bool submitted_blocking{};

std::int16_t read_sample(const std::vector<std::uint8_t>& memory,
                         std::size_t offset) {
  std::int16_t sample{};
  std::memcpy(&sample, memory.data() + offset, sizeof(sample));
  return sample;
}

} // namespace

namespace refract::host {

bool submit_audio(const std::int16_t* samples, std::uint32_t frame_count,
                  std::uint32_t channel, bool blocking, std::uint32_t) {
  submitted_audio.assign(samples, samples + frame_count * 2U);
  submitted_channel = channel;
  submitted_blocking = blocking;
  return true;
}

void sleep_microseconds(std::uint32_t) {}

} // namespace refract::host

int main() {
  CHECK(refract::host::audio_callback_timeout_microseconds(512U, 44100U) ==
        100000U);
  CHECK(refract::host::audio_callback_timeout_microseconds(2048U, 44100U) ==
        185759U);
  CHECK(refract::host::audio_callback_timeout_microseconds(32768U, 44100U) ==
        500000U);
  CHECK(refract::host::audio_callback_timeout_microseconds(32768U, 0U) ==
        100000U);
  CHECK(audio_state::scale_sample(16384, audio_state::maximum_volume) ==
        16384);
  CHECK(audio_state::scale_sample(16384, audio_state::maximum_volume / 2U) ==
        8192);
  CHECK(audio_state::scale_sample(-16384, audio_state::maximum_volume / 2U) ==
        -8192);
  CHECK(audio_state::scale_sample(32767, 0U) == 0);

  std::array<std::uint8_t, 16> vag_block{};
  vag_block[1] = 1U;
  std::fill(vag_block.begin() + 2, vag_block.end(), 0x21U);
  const auto decoded = sas_state::decode_vag(vag_block.data(), vag_block.size());
  CHECK(decoded.size() == 28U);
  CHECK(decoded[0] == 4096);
  CHECK(decoded[1] == 8192);
  CHECK(sas_state::decode_vag(vag_block.data(), 15U).empty());
  std::array<std::uint8_t, 0x40> headered_vag{};
  headered_vag[0] = 'V';
  headered_vag[1] = 'A';
  headered_vag[2] = 'G';
  headered_vag[3] = 'p';
  std::copy(vag_block.begin(), vag_block.end(), headered_vag.begin() + 0x30U);
  CHECK(sas_state::decode_vag(headered_vag.data(), headered_vag.size()) ==
        decoded);

  constexpr std::uint32_t memory_base = 0x08800000U;
  constexpr std::uint32_t core_address = memory_base + 0x100U;
  constexpr std::uint32_t source_address = memory_base + 0x800U;
  constexpr std::uint32_t output_address = memory_base + 0x1000U;
  std::vector<std::uint8_t> memory(0x2000U);
  std::memcpy(memory.data() + (source_address - memory_base), vag_block.data(),
              vag_block.size());
  psprecomp::State state{};
  state.memory = memory.data();
  state.memory_size = memory.size();
  state.memory_base = memory_base;

  constexpr std::uint32_t pcm_address = memory_base + 0x600U;
  const std::array<std::int16_t, 2> stereo_samples{16384, -16384};
  std::memcpy(memory.data() + (pcm_address - memory_base),
              stereo_samples.data(), sizeof(stereo_samples));
  audio_state::reset_for_tests();
  CHECK(audio_state::reserve(-1, 1U, 7U) ==
        static_cast<std::int32_t>(audio_state::invalid_format));
  CHECK(audio_state::reserve(-1, 1U, audio_state::stereo_format) == 0);
  CHECK(audio_state::output(state, 0U, audio_state::maximum_volume,
                            audio_state::maximum_volume / 2U, pcm_address,
                            false) == 1U);
  CHECK(submitted_audio.size() == 2U);
  CHECK(submitted_audio[0] == 16384);
  CHECK(submitted_audio[1] == -8192);
  CHECK(submitted_channel == 0U);
  CHECK(!submitted_blocking);
  CHECK(audio_state::output(state, 8U, audio_state::maximum_volume,
                            audio_state::maximum_volume, pcm_address, false) ==
        audio_state::invalid_channel);
  CHECK(audio_state::output(state, 0U, audio_state::maximum_volume + 1U,
                            audio_state::maximum_volume, pcm_address, false) ==
        audio_state::invalid_volume);
  CHECK(audio_state::output(state, 0U, audio_state::maximum_volume,
                            audio_state::maximum_volume,
                            memory_base + memory.size(), false) == 0xffffffffU);
  CHECK(audio_state::release(0U));

  const std::array<std::int16_t, 2> mono_samples{1000, -2000};
  std::memcpy(memory.data() + (pcm_address - memory_base), mono_samples.data(),
              sizeof(mono_samples));
  CHECK(audio_state::reserve(-1, 2U, audio_state::mono_format) == 0);
  CHECK(audio_state::output(state, 0U, audio_state::maximum_volume,
                            audio_state::maximum_volume / 2U, pcm_address,
                            false) == 2U);
  CHECK((submitted_audio ==
         std::vector<std::int16_t>{1000, 500, -2000, -1000}));

  sas_state::reset_for_tests();
  CHECK(sas_state::initialize(state, core_address, 64U, 32U, 0U, 44100U) ==
        0U);
  CHECK(sas_state::set_voice(core_address, 0, state, source_address,
                             static_cast<std::int32_t>(vag_block.size()), 0) ==
        0U);
  CHECK(sas_state::key_on(core_address, 0) == 0U);
  CHECK(sas_state::mix(state, core_address, output_address, false) == 0U);
  const auto output_offset = output_address - memory_base;
  CHECK(read_sample(memory, output_offset) == 4096);
  CHECK(read_sample(memory, output_offset + 2U) == 4096);
  CHECK(read_sample(memory, output_offset + 4U) == 8192);
  CHECK(read_sample(memory, output_offset + 6U) == 8192);
  CHECK((sas_state::end_flags(core_address) & 1U) != 0U);

  constexpr std::uint32_t pcm_voice_address = memory_base + 0x900U;
  const std::array<std::int16_t, 4> pcm_voice{1000, 2000, 3000, 4000};
  std::memcpy(memory.data() + pcm_voice_address - memory_base,
              pcm_voice.data(), sizeof(pcm_voice));
  sas_state::reset_for_tests();
  CHECK(sas_state::initialize(state, core_address, 64U, 32U, 0U, 44100U) ==
        0U);
  CHECK(sas_state::set_voice_pcm(core_address, 1, state, pcm_voice_address,
                                 static_cast<std::int32_t>(pcm_voice.size()),
                                 -1) == 0U);
  CHECK((sas_state::end_flags(core_address) & (1U << 1U)) != 0U);
  CHECK(sas_state::set_volume(core_address, 1,
                              static_cast<std::int32_t>(sas_state::max_volume),
                              static_cast<std::int32_t>(sas_state::max_volume /
                                                        2U),
                              0, 0) == 0U);
  CHECK(sas_state::set_pitch(core_address, 1, 0x2000U) == 0U);
  CHECK(sas_state::key_on(core_address, 1) == 0U);
  CHECK(sas_state::mix(state, core_address, output_address, false) == 0U);
  CHECK(read_sample(memory, output_offset) == 1000);
  CHECK(read_sample(memory, output_offset + 2U) == 500);
  CHECK(read_sample(memory, output_offset + 4U) == 3000);
  CHECK(read_sample(memory, output_offset + 6U) == 1500);
  CHECK((sas_state::end_flags(core_address) & (1U << 1U)) != 0U);

  const std::array<std::int16_t, 64> constant_pcm = [] {
    std::array<std::int16_t, 64> result{};
    result.fill(500);
    return result;
  }();
  std::memcpy(memory.data() + pcm_voice_address - memory_base,
              constant_pcm.data(), sizeof(constant_pcm));
  std::array<std::int16_t, 128> preserved_output{};
  preserved_output.fill(100);
  std::memcpy(memory.data() + output_address - memory_base,
              preserved_output.data(), sizeof(preserved_output));
  sas_state::reset_for_tests();
  CHECK(sas_state::initialize(state, core_address, 64U, 32U, 0U, 44100U) ==
        0U);
  CHECK(sas_state::set_voice_pcm(core_address, 3, state, pcm_voice_address,
                                 static_cast<std::int32_t>(constant_pcm.size()),
                                 -1) == 0U);
  CHECK(sas_state::key_on(core_address, 3) == 0U);
  CHECK(sas_state::mix(state, core_address, output_address, true) == 0U);
  CHECK(read_sample(memory, output_offset) == 600);
  CHECK(read_sample(memory, output_offset + 2U) == 600);

  const std::array<std::int16_t, 2> looping_pcm{1000, 2000};
  std::memcpy(memory.data() + pcm_voice_address - memory_base,
              looping_pcm.data(), sizeof(looping_pcm));
  sas_state::reset_for_tests();
  CHECK(sas_state::initialize(state, core_address, 64U, 32U, 0U, 44100U) ==
        0U);
  CHECK(sas_state::set_voice_pcm(core_address, 2, state, pcm_voice_address,
                                 static_cast<std::int32_t>(looping_pcm.size()),
                                 1) == 0U);
  CHECK(sas_state::key_on(core_address, 2) == 0U);
  CHECK(sas_state::mix(state, core_address, output_address, false) == 0U);
  CHECK(read_sample(memory, output_offset) == 1000);
  CHECK(read_sample(memory, output_offset + 4U) == 2000);
  CHECK(read_sample(memory, output_offset + 8U) == 2000);
  CHECK((sas_state::end_flags(core_address) & (1U << 2U)) == 0U);
  CHECK(sas_state::set_pause(core_address, 1U << 2U, true) == 0U);
  CHECK((sas_state::pause_flags(core_address) & (1U << 2U)) != 0U);
  CHECK(sas_state::key_off(core_address, 2) == sas_state::voice_paused);
  CHECK(sas_state::set_pause(core_address, 1U << 2U, false) == 0U);
  CHECK(sas_state::key_off(core_address, 2) == 0U);
  CHECK((sas_state::end_flags(core_address) & (1U << 2U)) != 0U);
  return 0;
}
