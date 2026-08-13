#include "stubs/mpeg/media_engine.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
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

int main() {
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
  constexpr std::array<std::uint8_t, 4> audio_payload{0x0fU, 0xd0U, 6U, 7U};
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
  CHECK(audio->bytes == std::vector<std::uint8_t>(audio_payload.begin(),
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

  return 0;
}
