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

std::int16_t read_sample(const std::vector<std::uint8_t>& memory,
                         std::size_t offset) {
  std::int16_t sample{};
  std::memcpy(&sample, memory.data() + offset, sizeof(sample));
  return sample;
}

} // namespace

namespace refract::host {

bool submit_audio(const std::int16_t* samples, std::uint32_t frame_count,
                  std::uint32_t) {
  submitted_audio.assign(samples, samples + frame_count * 2U);
  return true;
}

void sleep_microseconds(std::uint32_t) {}

} // namespace refract::host

int main() {
  CHECK(audio_state::scale_sample(16384, audio_state::maximum_volume) ==
        16384);
  CHECK(audio_state::scale_sample(16384, audio_state::maximum_volume / 2U) ==
        8192);

  std::array<std::uint8_t, 16> vag_block{};
  vag_block[1] = 1U;
  std::fill(vag_block.begin() + 2, vag_block.end(), 0x21U);
  const auto decoded = sas_state::decode_vag(vag_block.data(), vag_block.size());
  CHECK(decoded.size() == 28U);
  CHECK(decoded[0] == 4096);
  CHECK(decoded[1] == 8192);

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
  CHECK(audio_state::reserve(-1, 1U, audio_state::stereo_format) == 0);
  CHECK(audio_state::output(state, 0U, audio_state::maximum_volume,
                            audio_state::maximum_volume / 2U, pcm_address,
                            false) == 1U);
  CHECK(submitted_audio.size() == 2U);
  CHECK(submitted_audio[0] == 16384);
  CHECK(submitted_audio[1] == -8192);

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
  return 0;
}
