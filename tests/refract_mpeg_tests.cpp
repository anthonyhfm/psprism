#include "stubs/mpeg/media_engine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
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
  auto ignored_audio_payload = audio_payload;
  std::fill(ignored_audio_payload.begin() + 8,
            ignored_audio_payload.end(), 0x7eU);
  const auto ignored_audio_packet =
      pes(0xbdU, ignored_audio_payload, 92000, 0x91U);
  const auto audio_packet = pes(0xbdU, audio_payload, 94180, 0x90U);
  video_packet.insert(video_packet.end(), ignored_audio_packet.begin(),
                      ignored_audio_packet.end());
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
  CHECK(audio->channel == 0U);
  CHECK(engine.buffered_bytes() == 0U);

  // PSMF commonly splits H.264 access units across PES packets and can place
  // several access units in one packet.  Annex-B AUD NALs delimit the units;
  // returning each PES payload separately corrupts every frame after startup.
  mpeg_state::MediaEngine framed_engine(8192U);
  const auto framed_video = framed_engine.register_stream(0U, 0U);
  const std::vector<std::uint8_t> first_au{
      0U, 0U, 1U, 9U, 0x10U, 0U, 0U, 1U, 1U, 0xaaU, 0xbbU};
  const std::vector<std::uint8_t> second_au{
      0U, 0U, 1U, 9U, 0x10U, 0U, 0U, 1U, 1U, 0xccU, 0xddU, 0xeeU};
  const std::vector<std::uint8_t> sentinel_au{
      0U, 0U, 1U, 9U, 0x10U, 0U, 0U, 1U, 1U};
  auto first_fragment = pes(
      0xe0U, std::span<const std::uint8_t>(first_au).first(7U), 1000);
  std::vector<std::uint8_t> middle_payload(first_au.begin() + 7,
                                           first_au.end());
  middle_payload.insert(middle_payload.end(), second_au.begin(),
                        second_au.begin() + 6);
  auto middle_fragment = pes(0xe0U, middle_payload, -1);
  std::vector<std::uint8_t> last_payload(second_au.begin() + 6,
                                         second_au.end());
  last_payload.insert(last_payload.end(), sentinel_au.begin(),
                      sentinel_au.end());
  auto last_fragment = pes(0xe0U, last_payload, -1);
  first_fragment.insert(first_fragment.end(), middle_fragment.begin(),
                        middle_fragment.end());
  first_fragment.insert(first_fragment.end(), last_fragment.begin(),
                        last_fragment.end());
  CHECK(framed_engine.append_packets(first_fragment));
  const auto framed_first = framed_engine.next_access_unit(framed_video);
  CHECK(framed_first.has_value());
  CHECK(framed_first->bytes == first_au);
  CHECK(framed_first->pts == 1000);
  const auto framed_second = framed_engine.next_access_unit(framed_video);
  CHECK(framed_second.has_value());
  CHECK(framed_second->bytes == second_au);
  CHECK(framed_second->pts == 4003);
  CHECK(!framed_engine.next_access_unit(framed_video));
  CHECK(framed_engine.queue_stats().video_staging_bytes ==
        sentinel_au.size());

  // The byte capacity is the lossless backpressure boundary.  A unit-count
  // limit must not silently discard later PES packets before the guest asks
  // for them.
  mpeg_state::MediaEngine queued_engine(8192U);
  const auto queued_video = queued_engine.register_stream(0U, 0U);
  constexpr std::size_t queued_unit_count = 96U;
  std::vector<std::uint8_t> queued_packets;
  for (std::size_t index = 0U; index < queued_unit_count; ++index) {
    const std::array<std::uint8_t, 3> payload{
        static_cast<std::uint8_t>(index), 0xaaU, 0x55U};
    const auto packet = pes(0xe0U, payload, static_cast<std::int64_t>(index));
    queued_packets.insert(queued_packets.end(), packet.begin(), packet.end());
  }
  CHECK(queued_engine.append_packets(queued_packets));
  CHECK(queued_engine.queue_stats().video_units == 64U);
  CHECK(queued_engine.queue_stats().encoded_bytes != 0U);
  for (std::size_t index = 0U; index < queued_unit_count; ++index) {
    const auto unit = queued_engine.next_access_unit(queued_video);
    CHECK(unit.has_value());
    CHECK(unit->bytes.size() == 3U);
    CHECK(unit->bytes[0] == static_cast<std::uint8_t>(index));
  }
  CHECK(!queued_engine.next_access_unit(queued_video));

  mpeg_state::MediaEngine bounded_engine(2048U);
  const auto bounded_video = bounded_engine.register_stream(0U, 0U);
  std::vector<std::uint8_t> large_payload(1500U, 0x5aU);
  const auto large_packet = pes(0xe0U, large_payload, 1);
  CHECK(bounded_engine.append_packets(large_packet));
  CHECK(!bounded_engine.append_packets(large_packet));
  CHECK(bounded_engine.next_access_unit(bounded_video).has_value());
  CHECK(bounded_engine.append_packets(large_packet));
  CHECK(bounded_engine.next_access_unit(bounded_video).has_value());

  CHECK(engine.set_video_mode(0U));
  CHECK(engine.video_pixel_format() == 0U);
  CHECK(!engine.set_video_mode(4U));
  CHECK(engine.set_decode_mode(-1));
  CHECK(engine.decode_mode() == -1);
  CHECK(engine.set_decode_mode(2));
  CHECK(engine.decode_mode() == 2);
  CHECK(!engine.set_decode_mode(-2));
  CHECK(!engine.set_decode_mode(3));
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
    std::uint32_t video_frames{};
    std::uint32_t audio_units{};
    std::uint64_t video_decode_microseconds{};
    std::uint64_t audio_decode_microseconds{};
    mpeg_state::VideoFrameInfo frame_info;
    std::array<std::int16_t, 4096> pcm{};
    const auto inspection_end = std::min<std::size_t>(
        pmf.size(), real_header->stream_offset + 8U * 1024U * 1024U);
    constexpr std::uint32_t benchmark_frames = 300U;
    const auto benchmark_start = std::chrono::steady_clock::now();
    while (cursor < inspection_end && video_frames < benchmark_frames) {
      const auto count = std::min<std::size_t>(2048U, pmf.size() - cursor);
      CHECK(real_engine.append_packets(
          std::span<const std::uint8_t>(pmf).subspan(cursor, count)));
      cursor += count;
      while (real_engine.next_access_unit(real_video)) {
        ++video_units;
        const auto decode_start = std::chrono::steady_clock::now();
        CHECK(real_engine.decode_pending_video(frame_buffer, 512U, 3U,
                                               frame_info));
        const auto decode_end = std::chrono::steady_clock::now();
        video_decode_microseconds += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                decode_end - decode_start)
                .count());
        if (frame_info.produced) ++video_frames;
        if (video_frames >= benchmark_frames) break;
      }
      while (real_engine.next_access_unit(real_audio)) {
        ++audio_units;
        std::uint32_t samples{};
        const auto decode_start = std::chrono::steady_clock::now();
        CHECK(real_engine.decode_pending_audio(pcm, samples));
        const auto decode_end = std::chrono::steady_clock::now();
        audio_decode_microseconds += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                decode_end - decode_start)
                .count());
        CHECK(samples == 2048U);
      }
    }
    const auto benchmark_end = std::chrono::steady_clock::now();
    CHECK(video_units != 0U);
    CHECK(frame_info.produced);
    CHECK(video_frames != 0U);
    CHECK(frame_info.width == real_header->width);
    CHECK(frame_info.height == real_header->height);
    std::cout << "decoded_video_units=" << video_units
              << " video_frames=" << video_frames
              << " audio_units=" << audio_units << " frame="
              << frame_info.width << 'x' << frame_info.height
              << " video_us_per_unit="
              << (video_decode_microseconds / video_units)
              << " audio_us_per_unit="
              << (audio_units == 0U ? 0U
                                    : audio_decode_microseconds / audio_units)
              << " pipeline_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     benchmark_end - benchmark_start)
                     .count()
              << '\n';
  }

  return 0;
}
