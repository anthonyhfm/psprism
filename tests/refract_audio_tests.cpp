#include <psprecomp/runtime.hpp>

#include "host/host.hpp"
#include "host/audio_engine.hpp"
#include "stubs/audio/audio_state.hpp"
#include "stubs/audio/sas_state.hpp"

#include <algorithm>
#include <array>
#include <chrono>
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
bool submit_succeeds{true};
std::array<std::uint32_t, 9> fake_queued_frames{};
refract::host::AudioTelemetry fake_audio_telemetry{};

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
  if (submit_succeeds) fake_queued_frames[channel] = frame_count;
  return submit_succeeds;
}

void sleep_microseconds(std::uint32_t) {}

} // namespace refract::host

int main() {
  refract::host::set_audio_queue_callbacks(
      [](std::uint32_t channel) {
        return channel < fake_queued_frames.size()
                   ? fake_queued_frames[channel]
                   : 0U;
      },
      [](std::uint32_t channel) {
        if (channel < fake_queued_frames.size())
          fake_queued_frames[channel] = 0U;
      });
  refract::host::set_audio_telemetry_callback(
      [] { return fake_audio_telemetry; });
  refract::host::set_audio_clock_frames_callback(
      [] { return fake_audio_telemetry.consumed_frames; });
  fake_audio_telemetry.consumed_frames = 44100U;
  CHECK(refract::host::audio_clock_frames() == 44100U);
  CHECK(refract::host::audio_clock_microseconds() == 1000000U);
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

  {
    refract::host::AudioEngine engine;
    const std::array<std::int16_t, 8> first{100, 200, 300, 400,
                                            500, 600, 700, 800};
    CHECK(engine.submit(first.data(), 4U, 0U, false,
                        std::chrono::microseconds(0)) ==
          refract::host::AudioEngine::SubmitResult::submitted);
    CHECK(engine.queued_frames(0U) == 4U);
    CHECK(engine.submit(first.data(), 4U, 0U, false,
                        std::chrono::microseconds(0)) ==
          refract::host::AudioEngine::SubmitResult::busy);
    std::array<std::int16_t, 4> mixed{};
    CHECK(engine.consume(mixed.data(), 2U) == 2U);
    CHECK((mixed == std::array<std::int16_t, 4>{100, 200, 300, 400}));
    CHECK(engine.queued_frames(0U) == 2U);
    const std::array<std::int16_t, 4> second{32767, -32768, 100, -100};
    CHECK(engine.submit(second.data(), 2U, 1U, false,
                        std::chrono::microseconds(0)) ==
          refract::host::AudioEngine::SubmitResult::submitted);
    CHECK(engine.consume(mixed.data(), 2U) == 2U);
    CHECK(mixed[0] == 32767);
    CHECK(mixed[1] == -32168);
    CHECK(mixed[2] == 800);
    CHECK(mixed[3] == 700);
    CHECK(engine.queued_frames(0U) == 0U);
    CHECK(engine.queued_frames(1U) == 0U);
    const auto telemetry = engine.telemetry();
    CHECK(telemetry.submitted_frames == 6U);
    CHECK(telemetry.consumed_frames == 4U);
    CHECK(telemetry.queued_frames == 0U);
    CHECK(telemetry.peak_queued_frames == 4U);
    CHECK(telemetry.callback_count == 2U);
    CHECK(telemetry.overrun_submissions == 1U);
    CHECK(telemetry.dropped_frames == 4U);
    CHECK(telemetry.underrun_callbacks == 0U);
  }

  {
    refract::host::AudioEngine engine;
    constexpr std::array<std::uint32_t, 4> callback_sizes{64U, 128U, 257U,
                                                          512U};
    constexpr std::uint32_t submitted_per_iteration = 961U;
    std::array<std::int16_t, submitted_per_iteration * 2U> source{};
    source.fill(123);
    std::array<std::int16_t, 512U * 2U> output{};
    constexpr std::uint32_t iterations = 200U;
    for (std::uint32_t iteration = 0U; iteration < iterations; ++iteration) {
      CHECK(engine.submit(source.data(), submitted_per_iteration, 0U, false,
                          std::chrono::microseconds(0)) ==
            refract::host::AudioEngine::SubmitResult::submitted);
      for (const auto callback_size : callback_sizes)
        CHECK(engine.consume(output.data(), callback_size) == callback_size);
    }
    auto telemetry = engine.telemetry();
    CHECK(telemetry.submitted_frames ==
          static_cast<std::uint64_t>(submitted_per_iteration) * iterations);
    CHECK(telemetry.consumed_frames == telemetry.submitted_frames);
    CHECK(telemetry.callback_count == callback_sizes.size() * iterations);
    CHECK(telemetry.underrun_callbacks == 0U);
    CHECK(telemetry.queued_frames == 0U);
    CHECK(telemetry.peak_queued_frames == submitted_per_iteration);

    CHECK(engine.consume(output.data(), 257U) == 0U);
    telemetry = engine.telemetry();
    CHECK(telemetry.underrun_callbacks == 1U);
    CHECK(telemetry.underrun_frames == 257U);

    CHECK(engine.submit(source.data(), 100U, 0U, false,
                        std::chrono::microseconds(0)) ==
          refract::host::AudioEngine::SubmitResult::submitted);
    CHECK(engine.submit(source.data(), 100U, 0U, false,
                        std::chrono::microseconds(0)) ==
          refract::host::AudioEngine::SubmitResult::busy);
    CHECK(engine.submit(source.data(), 100U, 0U, true,
                        std::chrono::microseconds(0)) ==
          refract::host::AudioEngine::SubmitResult::timeout);
    const auto clock_before_reset = engine.telemetry().consumed_frames;
    engine.device_reset();
    telemetry = engine.telemetry();
    CHECK(telemetry.consumed_frames == clock_before_reset);
    CHECK(telemetry.queued_frames == 0U);
    CHECK(telemetry.overrun_submissions == 2U);
    CHECK(telemetry.dropped_frames == 300U);
    CHECK(telemetry.device_resets == 1U);

    CHECK(engine.submit(source.data(), 100U, 0U, false,
                        std::chrono::microseconds(0)) ==
          refract::host::AudioEngine::SubmitResult::submitted);
    CHECK(engine.submit(source.data(), 100U, 1U, false,
                        std::chrono::microseconds(0)) ==
          refract::host::AudioEngine::SubmitResult::submitted);
    CHECK(engine.consume(output.data(), 100U) == 100U);
    CHECK(engine.telemetry().consumed_frames == clock_before_reset + 100U);
  }

  {
    refract::host::AudioEngine engine;
    std::array<std::int16_t, 200U> stale{};
    stale.fill(111);
    std::array<std::int16_t, 200U> replacement{};
    replacement.fill(222);
    CHECK(engine.submit(stale.data(), 100U, 0U, false,
                        std::chrono::microseconds(0)) ==
          refract::host::AudioEngine::SubmitResult::submitted);
    CHECK(engine.submit(replacement.data(), 100U, 0U, true,
                        std::chrono::microseconds(0), true) ==
          refract::host::AudioEngine::SubmitResult::submitted);
    CHECK(engine.queued_frames(0U) == 100U);
    std::array<std::int16_t, 200U> recovered{};
    CHECK(engine.consume(recovered.data(), 100U) == 100U);
    CHECK(recovered == replacement);
    const auto telemetry = engine.telemetry();
    CHECK(telemetry.submitted_frames == 200U);
    CHECK(telemetry.overrun_submissions == 1U);
    CHECK(telemetry.dropped_frames == 100U);
  }

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
  std::array<std::uint8_t, 48> looping_vag{};
  looping_vag[1U] = 6U;
  looping_vag[16U + 1U] = 0U;
  looping_vag[32U + 1U] = 3U;
  std::fill(looping_vag.begin() + 2U, looping_vag.begin() + 16U, 0x11U);
  std::fill(looping_vag.begin() + 18U, looping_vag.begin() + 32U, 0x22U);
  std::fill(looping_vag.begin() + 34U, looping_vag.end(), 0x33U);
  const auto decoded_looping_vag =
      sas_state::decode_vag_details(looping_vag.data(), looping_vag.size());
  CHECK(decoded_looping_vag.samples.size() == 84U);
  CHECK(decoded_looping_vag.has_loop_start);
  CHECK(decoded_looping_vag.loop_start == 0U);
  CHECK(decoded_looping_vag.has_loop_end);
  CHECK(decoded_looping_vag.samples[28U] != decoded_looping_vag.samples[0U]);
  CHECK(sas_state::walk_envelope_curve(
            sas_state::max_envelope_height / 2U,
            sas_state::EnvelopeCurve::direct, 12345U) == 12345U);

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
  CHECK(audio_state::rest_length(0U) == 1U);
  submit_succeeds = false;
  CHECK(audio_state::output(state, 0U, audio_state::maximum_volume,
                            audio_state::maximum_volume, pcm_address, false) ==
        audio_state::channel_busy);
  submit_succeeds = true;
  CHECK(audio_state::output(state, 8U, audio_state::maximum_volume,
                            audio_state::maximum_volume, pcm_address, false) ==
        audio_state::invalid_channel);
  CHECK(audio_state::output(state, 0U, audio_state::maximum_api_volume + 1U,
                            audio_state::maximum_volume, pcm_address, false) ==
        audio_state::invalid_volume);
  CHECK(audio_state::output(state, 0U, audio_state::maximum_volume,
                            audio_state::maximum_volume,
                            memory_base + memory.size(), false) == 0xffffffffU);
  CHECK(audio_state::release(0U));
  CHECK(fake_queued_frames[0U] == 0U);

  CHECK(audio_state::reserve(-1, 1U, audio_state::stereo_format) == 0);
  CHECK(audio_state::output(state, 0U, audio_state::maximum_api_volume,
                            audio_state::maximum_volume, pcm_address, false) ==
        1U);
  CHECK(submitted_audio[0] == 32767);
  fake_queued_frames[0U] = 0U;
  CHECK(audio_state::output(state, 0U, -1, -1, pcm_address, false) == 1U);
  CHECK(submitted_audio[0] == 32767);
  fake_queued_frames[0U] = 0U;
  CHECK(audio_state::output(state, 0U, -1, audio_state::maximum_volume,
                            pcm_address, true, true) ==
        audio_state::invalid_volume);
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

  CHECK(audio_state::reserve_output2(16U) == audio_state::invalid_size);
  CHECK(audio_state::reserve_output2(64U) == 0U);
  CHECK(audio_state::reserve_output2(64U) == audio_state::already_reserved);
  CHECK(audio_state::output_output2(state, audio_state::maximum_volume,
                                    pcm_address, true) == 64U);
  CHECK(submitted_channel == audio_state::output2_channel);
  CHECK(submitted_blocking);
  CHECK(audio_state::output2_rest_length() == 64U);
  CHECK(audio_state::release_output2() == audio_state::already_reserved);
  fake_queued_frames[audio_state::output2_channel] = 0U;
  CHECK(audio_state::change_output2_length(32U) == 0U);
  CHECK(audio_state::release_output2() == 0U);
  CHECK(audio_state::output2_rest_length() == audio_state::not_reserved);

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

  preserved_output.fill(100);
  std::memcpy(memory.data() + output_address - memory_base,
              preserved_output.data(), sizeof(preserved_output));
  sas_state::reset_for_tests();
  CHECK(sas_state::initialize(state, core_address, 64U, 32U, 0U, 44100U) ==
        0U);
  CHECK(sas_state::mix(state, core_address, output_address, true,
                       sas_state::max_volume / 2U,
                       sas_state::max_volume / 4U) == 0U);
  CHECK(read_sample(memory, output_offset) == 50);
  CHECK(read_sample(memory, output_offset + 2U) == 25);

  std::fill(memory.begin() + output_offset,
            memory.begin() + output_offset + 64U * 4U * 2U, 0U);
  sas_state::reset_for_tests();
  CHECK(sas_state::initialize(state, core_address, 64U, 32U, 1U, 44100U) ==
        0U);
  CHECK(sas_state::set_voice_pcm(core_address, 6, state, pcm_voice_address,
                                 static_cast<std::int32_t>(constant_pcm.size()),
                                 -1) == 0U);
  CHECK(sas_state::set_volume(core_address, 6, sas_state::max_volume,
                              sas_state::max_volume / 2U,
                              sas_state::max_volume / 4U,
                              sas_state::max_volume / 8U) == 0U);
  CHECK(sas_state::key_on(core_address, 6) == 0U);
  CHECK(sas_state::mix(state, core_address, output_address, false) == 0U);
  CHECK(read_sample(memory, output_offset) == 500);
  CHECK(read_sample(memory, output_offset + 64U * 2U) == 250);
  CHECK(read_sample(memory, output_offset + 128U * 2U) == 125);
  CHECK(read_sample(memory, output_offset + 192U * 2U) == 62);

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

  const std::array<std::int16_t, 3> ramp_pcm{0, 1000, 2000};
  std::memcpy(memory.data() + pcm_voice_address - memory_base, ramp_pcm.data(),
              sizeof(ramp_pcm));
  sas_state::reset_for_tests();
  CHECK(sas_state::initialize(state, core_address, 64U, 32U, 0U, 44100U) ==
        0U);
  CHECK(sas_state::set_voice_pcm(core_address, 4, state, pcm_voice_address,
                                 static_cast<std::int32_t>(ramp_pcm.size()),
                                 -1) == 0U);
  CHECK(sas_state::set_pitch(core_address, 4, 0x0800U) == 0U);
  CHECK(sas_state::key_on(core_address, 4) == 0U);
  CHECK(sas_state::mix(state, core_address, output_address, false) == 0U);
  CHECK(read_sample(memory, output_offset) == 0);
  CHECK(read_sample(memory, output_offset + 4U) == 500);
  CHECK(read_sample(memory, output_offset + 8U) == 1000);

  const std::array<std::int16_t, 128> envelope_pcm = [] {
    std::array<std::int16_t, 128> result{};
    result.fill(1000);
    return result;
  }();
  std::memcpy(memory.data() + pcm_voice_address - memory_base,
              envelope_pcm.data(), sizeof(envelope_pcm));
  sas_state::reset_for_tests();
  CHECK(sas_state::initialize(state, core_address, 64U, 32U, 0U, 44100U) ==
        0U);
  CHECK(sas_state::set_voice_pcm(core_address, 5, state, pcm_voice_address,
                                 static_cast<std::int32_t>(envelope_pcm.size()),
                                 -1) == 0U);
  CHECK(sas_state::set_adsr_rates(
            core_address, 5, 0xfU,
            std::array<std::uint32_t, 4>{sas_state::max_envelope_height, 0U,
                                         0U,
                                         sas_state::max_envelope_height / 2U}) ==
        0U);
  CHECK(sas_state::key_on(core_address, 5) == 0U);
  CHECK(sas_state::mix(state, core_address, output_address, false) == 0U);
  CHECK(read_sample(memory, output_offset) == 1000);
  CHECK(sas_state::key_off(core_address, 5) == 0U);
  CHECK(sas_state::mix(state, core_address, output_address, false) == 0U);
  CHECK(read_sample(memory, output_offset) == 500);
  CHECK(read_sample(memory, output_offset + 4U) == 0);
  CHECK((sas_state::end_flags(core_address) & (1U << 5U)) != 0U);
  return 0;
}
