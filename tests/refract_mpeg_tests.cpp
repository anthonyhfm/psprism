#include "stubs/mpeg/media_engine.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <span>
#include <vector>

#define CHECK(expression)                                                   \
  do {                                                                      \
    if (!(expression)) {                                                    \
      std::cerr << "CHECK failed at " << __FILE__ << ':' << __LINE__       \
                << ": " #expression << '\n';                              \
      return 1;                                                             \
    }                                                                       \
  } while (false)

namespace {

void write_be32(std::uint8_t* output, std::uint32_t value) {
  output[0] = static_cast<std::uint8_t>(value >> 24U);
  output[1] = static_cast<std::uint8_t>(value >> 16U);
  output[2] = static_cast<std::uint8_t>(value >> 8U);
  output[3] = static_cast<std::uint8_t>(value);
}

std::array<std::uint8_t, 5> timestamp(std::int64_t value) {
  return {
      static_cast<std::uint8_t>(0x21U | ((value >> 29U) & 0x0eU)),
      static_cast<std::uint8_t>(value >> 22U),
      static_cast<std::uint8_t>(((value >> 14U) & 0xfeU) | 1U),
      static_cast<std::uint8_t>(value >> 7U),
      static_cast<std::uint8_t>(((value << 1U) & 0xfeU) | 1U),
  };
}

std::vector<std::uint8_t> pes(std::uint8_t stream,
                              std::span<const std::uint8_t> payload,
                              std::int64_t pts, std::uint8_t channel = 0U) {
  std::vector<std::uint8_t> body{0x80U, 0x80U, 5U};
  const auto encoded_timestamp = timestamp(pts);
  body.insert(body.end(), encoded_timestamp.begin(), encoded_timestamp.end());
  if (stream == 0xbdU) {
    body.push_back(channel);
    body.insert(body.end(), {0U, 0U, 0U});
  }
  body.insert(body.end(), payload.begin(), payload.end());
  std::vector<std::uint8_t> result{0U, 0U, 1U, stream,
                                   static_cast<std::uint8_t>(body.size() >> 8U),
                                   static_cast<std::uint8_t>(body.size())};
  result.insert(result.end(), body.begin(), body.end());
  return result;
}

} // namespace

int main(int argc, char** argv) {
  std::array<std::uint8_t, 144> header{};
  std::copy_n("PSMF0015", 8U, header.begin());
  write_be32(header.data() + 8U, 2048U);
  write_be32(header.data() + 12U, 0x123400U);
  const auto first = timestamp(90000);
  const auto last = timestamp(180000);
  std::copy(first.begin(), first.end(), header.begin() + 0x54U);
  std::copy(last.begin(), last.end(), header.begin() + 0x5aU);
  header[142] = 30U;
  header[143] = 17U;
  const auto parsed = mpeg_state::parse_psmf_header(header);
  CHECK(parsed.has_value());
  CHECK(parsed->stream_offset == 2048U);
  CHECK(parsed->stream_size == 0x123400U);
  CHECK(parsed->first_timestamp == 90000);
  CHECK(parsed->last_timestamp == 180000);
  CHECK(parsed->width == 480U);
  CHECK(parsed->height == 272U);
  header[0] = 'X';
  CHECK(!mpeg_state::parse_psmf_header(header));

  mpeg_state::MediaEngine engine(8192U);
  const auto video_stream = engine.register_stream(0U, 0U);
  const auto audio_stream = engine.register_stream(1U, 0U);
  constexpr std::array<std::uint8_t, 5> video_payload{1U, 2U, 3U, 4U, 5U};
  std::array<std::uint8_t, 24> audio_payload{};
  audio_payload[0] = 0x0fU;
  audio_payload[1] = 0xd0U;
  audio_payload[2] = 0x20U;
  audio_payload[3] = 1U;
  for (std::size_t index = 8U; index < audio_payload.size(); ++index) {
    audio_payload[index] = static_cast<std::uint8_t>(index);
  }
  auto video_packet = pes(0xe0U, video_payload, 90000);
  const auto audio_packet = pes(0xbdU, audio_payload, 94180, 0x90U);
  video_packet.insert(video_packet.end(), audio_packet.begin(),
                      audio_packet.end());

  CHECK(engine.append_packets(std::span<const std::uint8_t>(video_packet).first(7U)));
  CHECK(!engine.next_access_unit(video_stream));
  CHECK(engine.append_packets(std::span<const std::uint8_t>(video_packet).subspan(7U)));
  const auto video = engine.next_access_unit(video_stream);
  CHECK(video.has_value());
  CHECK(video->bytes == std::vector<std::uint8_t>(video_payload.begin(),
                                                  video_payload.end()));
  CHECK(video->pts == 90000);
  const auto audio = engine.next_access_unit(audio_stream);
  CHECK(audio.has_value());
  CHECK(audio->bytes == std::vector<std::uint8_t>(audio_payload.begin() + 8,
                                                  audio_payload.end()));
  CHECK(audio->pts == 94180);
  CHECK(audio->channel == 0x90U);
  CHECK(engine.buffered_bytes() == 0U);

  CHECK(engine.set_video_mode(0U));
  CHECK(engine.video_pixel_format() == 0U);
  CHECK(!engine.set_video_mode(4U));
  CHECK(engine.unregister_stream(video_stream));
  CHECK(!engine.unregister_stream(video_stream));
  engine.flush();

  if (argc == 2) {
    std::ifstream input(argv[1], std::ios::binary);
    CHECK(input.good());
    std::vector<std::uint8_t> pmf((std::istreambuf_iterator<char>(input)),
                                  std::istreambuf_iterator<char>());
    const auto real_header = mpeg_state::parse_psmf_header(pmf);
    CHECK(real_header.has_value());
    CHECK(real_header->stream_offset < pmf.size());
    mpeg_state::MediaEngine real_engine(2U * 1024U * 1024U);
    const auto real_video = real_engine.register_stream(0U, 0U);
    const auto real_audio = real_engine.register_stream(1U, 0U);
    std::vector<std::uint8_t> frame_buffer(
        static_cast<std::size_t>(512U) * 272U * 4U);
    std::size_t cursor = real_header->stream_offset;
    std::uint32_t video_units{};
    std::uint32_t audio_units{};
    mpeg_state::VideoFrameInfo frame_info;
    const auto inspection_end = std::min<std::size_t>(
        pmf.size(), real_header->stream_offset + 8U * 1024U * 1024U);
    while (cursor < inspection_end &&
           (!frame_info.produced || audio_units == 0U)) {
      const auto count = std::min<std::size_t>(2048U, pmf.size() - cursor);
      CHECK(real_engine.append_packets(
          std::span<const std::uint8_t>(pmf).subspan(cursor, count)));
      cursor += count;
      while (real_engine.next_access_unit(real_video)) {
        ++video_units;
        if (!frame_info.produced) {
          CHECK(real_engine.decode_pending_video(frame_buffer, 512U, 3U,
                                                 frame_info));
        }
      }
      while (real_engine.next_access_unit(real_audio)) ++audio_units;
    }
    CHECK(video_units != 0U);
    CHECK(frame_info.produced);
    CHECK(frame_info.width == real_header->width);
    CHECK(frame_info.height == real_header->height);
    if (audio_units != 0U) {
      std::array<std::int16_t, 4096> pcm{};
      std::uint32_t samples{};
      CHECK(real_engine.decode_pending_audio(pcm, samples));
      CHECK(samples == 2048U);
    }
    std::cout << "decoded_video_units=" << video_units
              << " audio_units=" << audio_units << " frame="
              << frame_info.width << 'x' << frame_info.height << '\n';
  }

  return 0;
}
