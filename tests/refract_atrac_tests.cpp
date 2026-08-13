#include <psprecomp/runtime.hpp>

#include "../refract/third_party/at3_standalone/at3_decoders.h"
#include "stubs/audio/atrac_state.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) return __LINE__;                                         \
  } while (false)

namespace {

void write_u16(std::vector<std::uint8_t>& bytes, std::size_t offset,
               std::uint16_t value) {
  std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void write_u32(std::vector<std::uint8_t>& bytes, std::size_t offset,
               std::uint32_t value) {
  std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

std::vector<std::uint8_t> streaming_atrac_header() {
  std::vector<std::uint8_t> bytes(128U);
  write_u32(bytes, 0U, 0x46464952U);
  write_u32(bytes, 4U, 504U);
  write_u32(bytes, 8U, 0x45564157U);
  write_u32(bytes, 12U, 0x20746d66U);
  write_u32(bytes, 16U, 52U);
  write_u16(bytes, 20U, 0xfffeU);
  write_u16(bytes, 22U, 2U);
  write_u32(bytes, 24U, 44100U);
  write_u32(bytes, 28U, 16016U);
  write_u16(bytes, 32U, 744U);
  write_u32(bytes, 72U, 0x74636166U);
  write_u32(bytes, 76U, 8U);
  write_u32(bytes, 80U, 4096U);
  write_u32(bytes, 88U, 0x61746164U);
  write_u32(bytes, 92U, 416U);
  return bytes;
}

std::vector<std::uint8_t> looping_atrac_header() {
  std::vector<std::uint8_t> bytes(176U);
  write_u32(bytes, 0U, 0x46464952U);
  write_u32(bytes, 4U, 168U);
  write_u32(bytes, 8U, 0x45564157U);
  write_u32(bytes, 12U, 0x20746d66U);
  write_u32(bytes, 16U, 52U);
  write_u16(bytes, 20U, 0xfffeU);
  write_u16(bytes, 22U, 2U);
  write_u32(bytes, 24U, 44100U);
  write_u32(bytes, 28U, 16016U);
  write_u16(bytes, 32U, 16U);
  write_u32(bytes, 72U, 0x74636166U);
  write_u32(bytes, 76U, 8U);
  write_u32(bytes, 80U, 3000U);
  write_u32(bytes, 84U, 100U);
  write_u32(bytes, 88U, 0x6c706d73U);
  write_u32(bytes, 92U, 60U);
  write_u32(bytes, 124U, 1U);
  write_u32(bytes, 140U, 500U);
  write_u32(bytes, 144U, 1499U);
  write_u32(bytes, 152U, 3U);
  write_u32(bytes, 156U, 0x61746164U);
  write_u32(bytes, 160U, 12U);
  return bytes;
}

} // namespace

int main() {
  auto memory = streaming_atrac_header();
  constexpr std::uint32_t memory_base = 0x08800000U;
  psprecomp::State state{};
  state.memory = memory.data();
  state.memory_size = memory.size();
  state.memory_base = memory_base;

  atrac_state::Track track;
  CHECK(atrac_state::parse_riff(memory.data(), memory.size(), track));
  CHECK(track.codec_type == atrac_state::codec_atrac3plus);
  CHECK(track.channels == 2U);
  CHECK(track.block_align == 744U);
  CHECK(track.file_size == 512U);
  CHECK(track.data_offset == 96U);
  CHECK(track.data_size == 416U);

  auto loop_memory = looping_atrac_header();
  atrac_state::Track loop_track;
  CHECK(atrac_state::parse_riff(loop_memory.data(), loop_memory.size(),
                                loop_track));
  CHECK(loop_track.end_sample == 2999U);
  CHECK(loop_track.loop_start == 400U);
  CHECK(loop_track.loop_end == 1399U);
  CHECK(loop_track.loop_play_count == 3U);
  atrac_state::Decoder loop_decoder;
  loop_decoder.track = loop_track;
  loop_decoder.loop_count = 2;
  loop_decoder.decoded_samples = 1300U;
  CHECK(loop_decoder.limit_output_samples(200U) == 100U);
  CHECK(!loop_decoder.advance_output_position(99U));
  CHECK(loop_decoder.decoded_samples == 1399U);

  auto offset_header = memory;
  write_u32(offset_header, 84U, 100U);
  atrac_state::Decoder offset_decoder(atrac_state::codec_atrac3plus);
  CHECK(offset_decoder.set_data(offset_header.data(), offset_header.size(),
                                memory_base));
  CHECK(offset_decoder.skip_samples == 468U);
  CHECK(offset_decoder.next_samples() == 1580U);
  CHECK(offset_decoder.seek(200U));
  CHECK(offset_decoder.decoded_samples == 200U);
  CHECK(offset_decoder.skip_samples == 668U);
  CHECK(offset_decoder.next_samples() == 1380U);
  CHECK(!offset_decoder.seek(4097U));

  atrac_state::Decoder decoder(atrac_state::codec_atrac3plus);
  CHECK(decoder.set_data(memory.data(), memory.size(), memory_base));
  decoder.loop_count = -1;
  std::uint32_t sample_count{};
  bool reached_end{};
  CHECK(decoder.decode(nullptr, sample_count, reached_end) ==
        atrac_state::no_data);
  CHECK(decoder.data_cursor == track.data_offset);
  CHECK(!reached_end);
  std::uint32_t write_pointer{};
  std::uint32_t writable_bytes{};
  std::uint32_t read_offset{};
  decoder.stream_data_info(write_pointer, writable_bytes, read_offset);
  CHECK(write_pointer == memory_base);
  CHECK(writable_bytes == 96U);
  CHECK(read_offset == 128U);

  std::fill(memory.begin(), memory.begin() + 64U, 0x5aU);
  CHECK(decoder.add_stream_data(state, 64U) == atrac_state::success);
  CHECK(decoder.encoded.size() == 192U);
  CHECK(std::all_of(decoder.encoded.end() - 64, decoder.encoded.end(),
                    [](std::uint8_t value) { return value == 0x5aU; }));

  decoder.stream_data_info(write_pointer, writable_bytes, read_offset);
  CHECK(write_pointer == memory_base + 64U);
  CHECK(writable_bytes == 32U);
  CHECK(read_offset == 192U);
  CHECK(decoder.add_stream_data(state, 33U) == atrac_state::bad_data);
  return 0;
}
