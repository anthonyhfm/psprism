#include "media_engine.hpp"

#include "../../../third_party/at3_standalone/at3_decoders.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <unordered_map>

#if defined(REFRACT_HAS_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#endif

namespace mpeg_state {
namespace {

constexpr std::array<std::uint8_t, 4> psmf_magic{'P', 'S', 'M', 'F'};
constexpr std::size_t maximum_access_units_per_kind = 64U;
std::uint16_t read_be16(const std::uint8_t* value) {
  return static_cast<std::uint16_t>(value[0]) << 8U |
         static_cast<std::uint16_t>(value[1]);
}

std::uint32_t read_be32(const std::uint8_t* value) {
  return static_cast<std::uint32_t>(value[0]) << 24U |
         static_cast<std::uint32_t>(value[1]) << 16U |
         static_cast<std::uint32_t>(value[2]) << 8U |
         static_cast<std::uint32_t>(value[3]);
}

std::int64_t read_timestamp(const std::uint8_t* value) {
  return (static_cast<std::int64_t>(value[0] & 0x0eU) << 29U) |
         (static_cast<std::int64_t>(read_be16(value + 1U) >> 1U) << 15U) |
         static_cast<std::int64_t>(read_be16(value + 3U) >> 1U);
}

struct ParsedPes {
  std::size_t payload_offset{};
  std::int64_t pts{-1};
  std::int64_t dts{-1};
  std::uint8_t channel{};
};

std::optional<ParsedPes> parse_pes_header(std::span<const std::uint8_t> packet,
                                         bool private_stream) {
  if (packet.size() < 9U) return std::nullopt;
  ParsedPes result;
  std::size_t cursor = 6U;
  if ((packet[cursor] & 0xc0U) == 0x80U) {
    const auto flags = packet[cursor + 1U];
    const auto header_size = static_cast<std::size_t>(packet[cursor + 2U]);
    if (9U + header_size > packet.size()) return std::nullopt;
    cursor += 3U;
    if ((flags & 0x80U) != 0U && header_size >= 5U) {
      result.pts = read_timestamp(packet.data() + cursor);
      result.dts = result.pts;
      if ((flags & 0x40U) != 0U && header_size >= 10U) {
        result.dts = read_timestamp(packet.data() + cursor + 5U);
      }
    }
    cursor = 9U + header_size;
  } else {
    while (cursor < packet.size() && packet[cursor] == 0xffU) ++cursor;
    if (cursor >= packet.size()) return std::nullopt;
    if ((packet[cursor] & 0xe0U) == 0x20U) {
      if (cursor + 5U > packet.size()) return std::nullopt;
      result.pts = read_timestamp(packet.data() + cursor);
      result.dts = result.pts;
      cursor += 5U;
    } else if (packet[cursor] == 0x0fU) {
      ++cursor;
    }
  }
  if (private_stream) {
    if (cursor + 4U > packet.size()) return std::nullopt;
    result.channel = packet[cursor++];
    cursor += 3U;
    if (result.channel >= 0xb0U && result.channel <= 0xbfU) {
      if (cursor >= packet.size()) return std::nullopt;
      ++cursor;
    }
  }
  result.payload_offset = cursor;
  return result;
}

std::unordered_map<std::uint32_t, std::shared_ptr<MediaEngine>>& engines() {
  static std::unordered_map<std::uint32_t, std::shared_ptr<MediaEngine>> value;
  return value;
}

std::unordered_map<std::uint32_t, std::uint32_t>& ringbuffer_gps() {
  static std::unordered_map<std::uint32_t, std::uint32_t> value;
  return value;
}

std::mutex& engines_mutex() {
  static std::mutex value;
  return value;
}

} // namespace

std::optional<PsmfHeader> parse_psmf_header(
    std::span<const std::uint8_t> data) {
  if (data.size() < 144U ||
      !std::equal(psmf_magic.begin(), psmf_magic.end(), data.begin())) {
    return std::nullopt;
  }
  const bool known_version =
      data[4] == '0' && data[5] == '0' && data[6] == '1' &&
      data[7] >= '2' && data[7] <= '5';
  const auto offset = read_be32(data.data() + 8U);
  if (!known_version || offset == 0U || (offset & 2047U) != 0U) {
    return std::nullopt;
  }
  PsmfHeader result;
  result.stream_offset = offset;
  result.stream_size = read_be32(data.data() + 12U);
  result.first_timestamp = read_timestamp(data.data() + 0x54U);
  result.last_timestamp = read_timestamp(data.data() + 0x5aU);
  result.width = static_cast<std::uint32_t>(data[142]) * 16U;
  result.height = static_cast<std::uint32_t>(data[143]) * 16U;
  return result;
}

struct MediaEngine::Implementation {
  struct Stream {
    StreamKind kind{StreamKind::other};
    std::uint32_t channel{};
  };

  struct AudioStaging {
    std::vector<std::uint8_t> bytes;
    std::int64_t next_pts{-1};
  };

  explicit Implementation(std::size_t requested_capacity)
      : capacity(std::max<std::size_t>(requested_capacity, 2048U)) {}

  ~Implementation() {
    if (audio_decoder != nullptr) atrac3p_free(audio_decoder);
#if defined(REFRACT_HAS_FFMPEG)
    if (parser != nullptr) av_parser_close(parser);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec);
    sws_freeContext(scaler);
#endif
  }

  std::size_t unit_count(StreamKind kind) const {
    return static_cast<std::size_t>(std::count_if(
        units.begin(), units.end(), [kind](const auto& candidate) {
          return candidate.first == kind;
        }));
  }

  static std::uint8_t normalized_audio_channel(std::uint8_t channel) {
    return channel >= 0x90U && channel <= 0x9fU
               ? static_cast<std::uint8_t>(channel & 0x0fU)
               : channel;
  }

  bool audio_channel_registered(std::uint8_t channel) const {
    const auto normalized = normalized_audio_channel(channel);
    return std::any_of(streams.begin(), streams.end(), [&](const auto& item) {
      return item.second.kind == StreamKind::audio &&
             item.second.channel == normalized;
    });
  }

  std::size_t audio_staging_size() const {
    std::size_t result{};
    for (const auto& staging : audio_staging)
      result += staging.second.bytes.size();
    return result;
  }

  static std::optional<std::size_t> find_video_aud(
      std::span<const std::uint8_t> bytes, std::size_t start = 0U) {
    for (auto index = start; index + 3U < bytes.size(); ++index) {
      if (bytes[index] != 0U || bytes[index + 1U] != 0U) continue;
      if (bytes[index + 2U] == 1U &&
          (bytes[index + 3U] & 0x1fU) == 9U)
        return index;
      if (index + 4U < bytes.size() && bytes[index + 2U] == 0U &&
          bytes[index + 3U] == 1U &&
          (bytes[index + 4U] & 0x1fU) == 9U)
        return index;
    }
    return std::nullopt;
  }

  void extract_video_access_units() {
    auto first = find_video_aud(video_bytes);
    if (!first) {
      if (video_bytes.size() > 4U) {
        video_bytes.erase(video_bytes.begin(), video_bytes.end() - 4);
      }
      return;
    }
    std::size_t cursor = *first;
    while (unit_count(StreamKind::video) <
           maximum_access_units_per_kind) {
      const auto remaining =
          std::span<const std::uint8_t>(video_bytes).subspan(cursor);
      const auto next = find_video_aud(remaining, 4U);
      if (!next) break;
      MediaAccessUnit unit;
      unit.bytes.assign(remaining.data(), remaining.data() + *next);
      unit.pts = video_next_pts;
      unit.dts = video_next_pts;
      unit.source_bytes = static_cast<std::uint32_t>(unit.bytes.size());
      last_video_pts = unit.pts;
      units.push_back({StreamKind::video, std::move(unit)});
      if (video_next_pts >= 0) video_next_pts += 3003;
      cursor += *next;
    }
    if (cursor != 0U) {
      if (cursor >= video_bytes.size()) {
        video_bytes.clear();
      } else {
        video_bytes.erase(video_bytes.begin(),
                          video_bytes.begin() +
                              static_cast<std::ptrdiff_t>(cursor));
      }
    }
  }

  bool queue_packet(std::size_t begin, std::size_t end, std::uint8_t code) {
    const bool video = code >= 0xe0U && code <= 0xefU;
    const bool audio = code == 0xbdU;
    if ((!video && !audio) || end <= begin) return true;
    if (video && unit_count(StreamKind::video) >=
                     maximum_access_units_per_kind) {
      return false;
    }
    const auto packet = std::span<const std::uint8_t>(encoded).subspan(
        begin, end - begin);
    const auto header = parse_pes_header(packet, audio);
    if (!header || header->payload_offset >= packet.size()) return true;
    const auto payload = packet.subspan(header->payload_offset);
    if (audio) {
      const auto channel = normalized_audio_channel(header->channel);
      if (!audio_channel_registered(channel)) return true;
      auto& staging = audio_staging[channel];
      if (staging.bytes.empty() && header->pts >= 0)
        staging.next_pts = header->pts;
      staging.bytes.insert(staging.bytes.end(), payload.begin(), payload.end());
      extract_audio_frames(channel);
      return true;
    }
    const auto aud = find_video_aud(payload);
    if (video_bytes.empty() && !aud) {
      // Some homebrew feeds one complete frame per PES without Annex-B AUDs.
      // Preserve that behavior while PSMF streams take the framed path below.
      MediaAccessUnit unit;
      unit.bytes.assign(payload.begin(), payload.end());
      unit.pts = header->pts;
      unit.dts = header->dts;
      unit.source_bytes = static_cast<std::uint32_t>(packet.size());
      units.push_back({StreamKind::video, std::move(unit)});
      return true;
    }
    if (video_next_pts < 0 && header->pts >= 0) video_next_pts = header->pts;
    video_bytes.insert(video_bytes.end(), payload.begin(), payload.end());
    extract_video_access_units();
    return true;
  }

  void extract_audio_frames(std::uint8_t channel) {
    auto found_staging = audio_staging.find(channel);
    if (found_staging == audio_staging.end()) return;
    auto& staging = found_staging->second;
    auto& audio_bytes = staging.bytes;
    constexpr std::array<std::uint8_t, 2> audio_header{0x0fU, 0xd0U};
    std::size_t cursor = 0U;
    for (;;) {
      const auto remaining =
          std::span<const std::uint8_t>(audio_bytes).subspan(cursor);
      const auto header = std::search(remaining.begin(), remaining.end(),
                                      audio_header.begin(), audio_header.end());
      if (header == remaining.end()) {
        if (remaining.size() > 1U) {
          const auto last = remaining.back();
          if (last == 0x0fU) {
            cursor = audio_bytes.size() - 1U;
          } else {
            cursor = audio_bytes.size();
          }
        }
        break;
      }
      cursor += static_cast<std::size_t>(
          std::distance(remaining.begin(), header));
      if (audio_bytes.size() - cursor < 4U) break;
      const auto code1 = audio_bytes[cursor + 2U];
      const auto code2 = audio_bytes[cursor + 3U];
      const auto frame_size =
          static_cast<std::size_t>(((code1 & 3U) << 8U) | (code2 * 8U)) + 16U;
      if (frame_size < 8U || frame_size > 8192U) {
        cursor += 1U;
        continue;
      }
      if (audio_bytes.size() - cursor < frame_size) break;
      if (unit_count(StreamKind::audio) >=
          maximum_access_units_per_kind) {
        break;
      }
      MediaAccessUnit unit;
      unit.bytes.assign(audio_bytes.begin() + cursor + 8U,
                        audio_bytes.begin() + cursor +
                            static_cast<std::ptrdiff_t>(frame_size));
      unit.pts = staging.next_pts;
      unit.dts = staging.next_pts;
      unit.source_bytes = static_cast<std::uint32_t>(frame_size);
      unit.channel = normalized_audio_channel(channel);
      unit.channels = code1 == 0x24U ? 1U : 2U;
      last_audio_pts = unit.pts;
      units.push_back({StreamKind::audio, std::move(unit)});
      if (staging.next_pts >= 0) staging.next_pts += 4180;
      cursor += frame_size;
    }
    if (cursor != 0U) {
      if (cursor >= audio_bytes.size()) {
        audio_bytes.clear();
      } else {
        audio_bytes.erase(audio_bytes.begin(),
                          audio_bytes.begin() +
                              static_cast<std::ptrdiff_t>(cursor));
      }
    }
  }

  void demux() {
    extract_video_access_units();
    for (auto& staging : audio_staging) {
      if (!staging.second.bytes.empty()) extract_audio_frames(staging.first);
    }
    std::size_t cursor = encoded_offset;
    std::size_t consumed = encoded_offset;
    while (cursor + 6U <= encoded.size()) {
      while (cursor + 3U < encoded.size() &&
             !(encoded[cursor] == 0U && encoded[cursor + 1U] == 0U &&
               encoded[cursor + 2U] == 1U)) {
        ++cursor;
      }
      if (cursor + 6U > encoded.size()) break;
      const auto code = encoded[cursor + 3U];
      if (code == 0xbaU) {
        if (cursor + 14U > encoded.size()) break;
        const auto size = 14U + (encoded[cursor + 13U] & 7U);
        if (cursor + size > encoded.size()) break;
        cursor += size;
        consumed = cursor;
        continue;
      }
      const auto payload_size = static_cast<std::size_t>(
          read_be16(encoded.data() + cursor + 4U));
      if (payload_size == 0U) {
        std::size_t next = cursor + 6U;
        while (next + 3U < encoded.size() &&
               !(encoded[next] == 0U && encoded[next + 1U] == 0U &&
                 encoded[next + 2U] == 1U)) {
          ++next;
        }
        if (next + 3U >= encoded.size()) break;
        if (!queue_packet(cursor, next, code)) break;
        cursor = next;
        consumed = cursor;
        continue;
      }
      const auto end = cursor + 6U + payload_size;
      if (end > encoded.size()) break;
      if (!queue_packet(cursor, end, code)) break;
      cursor = end;
      consumed = cursor;
    }
    encoded_offset = consumed;
    if (encoded_offset >= encoded.size()) {
      encoded.clear();
      encoded_offset = 0U;
    }
  }

  bool ensure_decoder() {
#if defined(REFRACT_HAS_FFMPEG)
    if (codec != nullptr) return true;
    const auto* decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (decoder == nullptr) return false;
    codec = avcodec_alloc_context3(decoder);
    packet = av_packet_alloc();
    frame = av_frame_alloc();
    if (codec == nullptr || packet == nullptr || frame == nullptr ||
        avcodec_open2(codec, decoder, nullptr) < 0) {
      return false;
    }
    return true;
#else
    return false;
#endif
  }

  mutable std::mutex mutex;
  std::size_t capacity{};
  std::vector<std::uint8_t> encoded;
  std::size_t encoded_offset{};
  std::vector<std::uint8_t> video_bytes;
  std::unordered_map<std::uint8_t, AudioStaging> audio_staging;
  std::deque<std::pair<StreamKind, MediaAccessUnit>> units;
  std::unordered_map<std::uint32_t, Stream> streams;
  std::uint32_t next_stream{1U};
  std::size_t queued_payload_bytes{};
  std::uint32_t frame_number{};
  std::int32_t decode_mode{};
  std::uint32_t pixel_format{3U};
  VideoFrameInfo last_frame{};
  std::optional<MediaAccessUnit> pending_video;
  std::optional<MediaAccessUnit> pending_audio;
  std::int64_t video_next_pts{-1};
  std::int64_t last_video_pts{-1};
  std::int64_t last_audio_pts{-1};
  ATRAC3PContext* audio_decoder{};
  std::size_t audio_block_align{};
  std::uint8_t audio_channels{};
#if defined(REFRACT_HAS_FFMPEG)
  AVCodecContext* codec{};
  AVCodecParserContext* parser{};
  AVPacket* packet{};
  AVFrame* frame{};
  SwsContext* scaler{};
  std::vector<std::uint8_t> rgba_scratch;
  std::vector<std::uint8_t> padded_video_packet;
#endif
};

MediaEngine::MediaEngine(std::size_t capacity_bytes)
    : implementation_(std::make_unique<Implementation>(capacity_bytes)) {}

MediaEngine::~MediaEngine() = default;

bool MediaEngine::append_packets(std::span<const std::uint8_t> bytes) {
  auto& impl = *implementation_;
  std::lock_guard lock(impl.mutex);
  if (bytes.empty()) return true;
  std::size_t queued{};
  for (const auto& item : impl.units) queued += item.second.source_bytes;
  const auto active_encoded = impl.encoded.size() - impl.encoded_offset;
  if (bytes.size() > impl.capacity ||
      active_encoded + impl.video_bytes.size() +
              impl.audio_staging_size() + queued >
          impl.capacity - bytes.size()) {
    return false;
  }
  if (impl.encoded_offset > 0U &&
      (impl.encoded_offset * 2U >= impl.encoded.size() ||
       impl.encoded_offset >= 65536U)) {
    impl.encoded.erase(
        impl.encoded.begin(),
        impl.encoded.begin() + static_cast<std::ptrdiff_t>(impl.encoded_offset));
    impl.encoded_offset = 0U;
  }
  impl.encoded.insert(impl.encoded.end(), bytes.begin(), bytes.end());
  impl.demux();
  return true;
}

std::uint32_t MediaEngine::register_stream(std::uint32_t type,
                                           std::uint32_t channel) {
  auto& impl = *implementation_;
  std::lock_guard lock(impl.mutex);
  const auto uid = impl.next_stream++;
  const auto kind = type == 0U ? StreamKind::video
                    : type == 1U || type == 15U ? StreamKind::audio
                                                : StreamKind::other;
  impl.streams.emplace(uid, Implementation::Stream{kind, channel});
  return uid;
}

bool MediaEngine::unregister_stream(std::uint32_t uid) {
  auto& impl = *implementation_;
  std::lock_guard lock(impl.mutex);
  return impl.streams.erase(uid) != 0U;
}

std::optional<StreamKind> MediaEngine::stream_kind(std::uint32_t uid) const {
  const auto& impl = *implementation_;
  std::lock_guard lock(impl.mutex);
  const auto found = impl.streams.find(uid);
  return found == impl.streams.end() ? std::nullopt
                                    : std::optional(found->second.kind);
}

MediaQueueStats MediaEngine::queue_stats() const {
  const auto& impl = *implementation_;
  std::lock_guard lock(impl.mutex);
  MediaQueueStats result;
  result.capacity_bytes = impl.capacity;
  result.encoded_bytes = impl.encoded.size() - impl.encoded_offset;
  result.video_staging_bytes = impl.video_bytes.size();
  result.audio_staging_bytes = impl.audio_staging_size();
  result.pending_video = impl.pending_video.has_value();
  result.pending_audio = impl.pending_audio.has_value();
  for (const auto& item : impl.units) {
    switch (item.first) {
      case StreamKind::video:
        ++result.video_units;
        break;
      case StreamKind::audio:
        ++result.audio_units;
        break;
      case StreamKind::other:
        ++result.other_units;
        break;
    }
  }
  return result;
}

bool MediaEngine::set_video_mode(std::uint32_t pixel_format) {
  if (pixel_format > 3U) return false;
  auto& impl = *implementation_;
  std::lock_guard lock(impl.mutex);
  impl.pixel_format = pixel_format;
  return true;
}

bool MediaEngine::set_decode_mode(std::int32_t mode) {
  if (mode < -1 || mode > 2) return false;
  auto& impl = *implementation_;
  std::lock_guard lock(impl.mutex);
  impl.decode_mode = mode;
  return true;
}

std::int32_t MediaEngine::decode_mode() const {
  const auto& impl = *implementation_;
  std::lock_guard lock(impl.mutex);
  return impl.decode_mode;
}

std::uint32_t MediaEngine::video_pixel_format() const {
  const auto& impl = *implementation_;
  std::lock_guard lock(impl.mutex);
  return impl.pixel_format;
}

VideoFrameInfo MediaEngine::last_video_frame() const {
  const auto& impl = *implementation_;
  std::lock_guard lock(impl.mutex);
  return impl.last_frame;
}

std::int64_t MediaEngine::last_video_pts() const {
  const auto& impl = *implementation_;
  std::lock_guard lock(impl.mutex);
  return impl.last_video_pts;
}

std::int64_t MediaEngine::last_audio_pts() const {
  const auto& impl = *implementation_;
  std::lock_guard lock(impl.mutex);
  return impl.last_audio_pts;
}

bool MediaEngine::is_video_end() const {
  const auto& impl = *implementation_;
  std::lock_guard lock(impl.mutex);
  return impl.units.empty() && impl.video_bytes.empty() &&
         (impl.encoded.size() <= impl.encoded_offset);
}

bool MediaEngine::is_audio_end() const {
  const auto& impl = *implementation_;
  std::lock_guard lock(impl.mutex);
  return impl.units.empty() && impl.audio_staging_size() == 0U &&
         (impl.encoded.size() <= impl.encoded_offset);
}

std::optional<MediaAccessUnit> MediaEngine::next_access_unit(
    std::uint32_t uid) {
  auto& impl = *implementation_;
  std::lock_guard lock(impl.mutex);
  const auto stream = impl.streams.find(uid);
  if (stream == impl.streams.end()) return std::nullopt;
  const auto found = std::find_if(
      impl.units.begin(), impl.units.end(), [&](const auto& candidate) {
        return candidate.first == stream->second.kind &&
               (candidate.first != StreamKind::audio ||
                candidate.second.channel == stream->second.channel);
      });
  if (found == impl.units.end()) return std::nullopt;
  auto result = std::move(found->second);
  impl.units.erase(found);
  if (stream->second.kind == StreamKind::video) {
    impl.pending_video = result;
    if (result.pts >= 0) impl.last_video_pts = result.pts;
  } else if (stream->second.kind == StreamKind::audio) {
    impl.pending_audio = result;
    if (result.pts >= 0) impl.last_audio_pts = result.pts;
  }
  impl.demux();
  return result;
}

namespace {

#if defined(REFRACT_HAS_FFMPEG)
std::uint16_t pack_565(std::uint8_t red, std::uint8_t green,
                       std::uint8_t blue) {
  return static_cast<std::uint16_t>((red >> 3U) | ((green >> 2U) << 5U) |
                                    ((blue >> 3U) << 11U));
}

std::uint16_t pack_5551(std::uint8_t red, std::uint8_t green,
                        std::uint8_t blue, std::uint8_t alpha) {
  return static_cast<std::uint16_t>((red >> 3U) | ((green >> 3U) << 5U) |
                                    ((blue >> 3U) << 10U) |
                                    ((alpha >> 7U) << 15U));
}

std::uint16_t pack_4444(std::uint8_t red, std::uint8_t green,
                        std::uint8_t blue, std::uint8_t alpha) {
  return static_cast<std::uint16_t>((red >> 4U) | ((green >> 4U) << 4U) |
                                    ((blue >> 4U) << 8U) |
                                    ((alpha >> 4U) << 12U));
}

bool copy_video_frame(AVFrame* frame, SwsContext*& scaler,
                      std::vector<std::uint8_t>& rgba,
                      std::span<std::uint8_t> output,
                      std::uint32_t frame_stride,
                      std::uint32_t pixel_format, std::uint32_t& frame_number,
                      VideoFrameInfo& last_frame, VideoFrameInfo& info) {
  const auto width = static_cast<std::uint32_t>(frame->width);
  const auto height = static_cast<std::uint32_t>(frame->height);
  if (width == 0U || height == 0U || frame_stride < width) return false;
  const auto bytes_per_pixel = pixel_format == 3U ? 4U : 2U;
  if (height > output.size() / frame_stride / bytes_per_pixel) return false;
  scaler = sws_getCachedContext(
      scaler, frame->width, frame->height,
      static_cast<AVPixelFormat>(frame->format), frame->width, frame->height,
      AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
  if (scaler == nullptr) return false;
  if (pixel_format == 3U) {
    std::uint8_t* destination[]{output.data()};
    const int destination_stride[]{static_cast<int>(frame_stride * 4U)};
    sws_scale(scaler, frame->data, frame->linesize, 0, frame->height,
              destination, destination_stride);
    info = {width, height, ++frame_number, true};
    last_frame = info;
    return true;
  }
  rgba.resize(static_cast<std::size_t>(width) * height * 4U);
  std::uint8_t* destination[]{rgba.data()};
  const int destination_stride[]{frame->width * 4};
  sws_scale(scaler, frame->data, frame->linesize, 0, frame->height,
            destination, destination_stride);
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      const auto source = (static_cast<std::size_t>(y) * width + x) * 4U;
      const auto target = (static_cast<std::size_t>(y) * frame_stride + x) *
                          bytes_per_pixel;
      const auto red = rgba[source];
      const auto green = rgba[source + 1U];
      const auto blue = rgba[source + 2U];
      const auto alpha = rgba[source + 3U];
      if (pixel_format == 3U) {
        output[target] = red;
        output[target + 1U] = green;
        output[target + 2U] = blue;
        output[target + 3U] = alpha;
      } else {
        const auto packed = pixel_format == 0U ? pack_565(red, green, blue)
                            : pixel_format == 1U
                                ? pack_5551(red, green, blue, alpha)
                                : pack_4444(red, green, blue, alpha);
        std::memcpy(output.data() + target, &packed, sizeof(packed));
      }
    }
  }
  info = {width, height, ++frame_number, true};
  last_frame = info;
  return true;
}
#endif

} // namespace

bool MediaEngine::decode_video(const MediaAccessUnit& access_unit,
                               std::span<std::uint8_t> output,
                               std::uint32_t frame_stride,
                               std::uint32_t pixel_format,
                               VideoFrameInfo& info) {
  auto& impl = *implementation_;
  std::lock_guard lock(impl.mutex);
  info = {};
#if defined(REFRACT_HAS_FFMPEG)
  if (!impl.ensure_decoder()) return false;
  if (access_unit.bytes.empty() ||
      access_unit.bytes.size() >
          static_cast<std::size_t>(std::numeric_limits<int>::max()))
    return false;
  impl.padded_video_packet.assign(
      access_unit.bytes.size() + AV_INPUT_BUFFER_PADDING_SIZE, 0U);
  std::copy(access_unit.bytes.begin(), access_unit.bytes.end(),
            impl.padded_video_packet.begin());
  av_packet_unref(impl.packet);
  impl.packet->data = impl.padded_video_packet.data();
  impl.packet->size = static_cast<int>(access_unit.bytes.size());
  impl.packet->pts = access_unit.pts;
  impl.packet->dts = access_unit.dts;
  const auto send_result = avcodec_send_packet(impl.codec, impl.packet);
  impl.packet->data = nullptr;
  impl.packet->size = 0;
  if (send_result < 0) return false;
  const auto receive_result = avcodec_receive_frame(impl.codec, impl.frame);
  if (receive_result == AVERROR(EAGAIN) || receive_result == AVERROR_EOF)
    return true;
  if (receive_result < 0) return false;
  return copy_video_frame(impl.frame, impl.scaler, impl.rgba_scratch, output,
                          frame_stride, pixel_format, impl.frame_number,
                          impl.last_frame, info);
#else
  static_cast<void>(access_unit);
  static_cast<void>(output);
  static_cast<void>(frame_stride);
  static_cast<void>(pixel_format);
  return false;
#endif
}

bool MediaEngine::decode_pending_video(std::span<std::uint8_t> output,
                                       std::uint32_t frame_stride,
                                       std::uint32_t pixel_format,
                                       VideoFrameInfo& info) {
  std::optional<MediaAccessUnit> pending;
  {
    auto& impl = *implementation_;
    std::lock_guard lock(impl.mutex);
    if (!impl.pending_video) {
      info = {};
      return false;
    }
    pending = std::move(impl.pending_video);
    impl.pending_video.reset();
  }
  return decode_video(*pending, output, frame_stride, pixel_format, info);
}

std::optional<MediaAccessUnit> MediaEngine::take_pending_audio() {
  auto& impl = *implementation_;
  std::lock_guard lock(impl.mutex);
  auto result = std::move(impl.pending_audio);
  impl.pending_audio.reset();
  return result;
}

bool MediaEngine::decode_pending_audio(std::span<std::int16_t> output,
                                       std::uint32_t& sample_count) {
  auto& impl = *implementation_;
  std::lock_guard lock(impl.mutex);
  sample_count = 0U;
  if (!impl.pending_audio || output.size() < 4096U ||
      impl.pending_audio->bytes.empty()) {
    return false;
  }
  const auto& unit = *impl.pending_audio;
  if (impl.audio_decoder == nullptr ||
      impl.audio_block_align != unit.bytes.size() ||
      impl.audio_channels != unit.channels) {
    if (impl.audio_decoder != nullptr) atrac3p_free(impl.audio_decoder);
    auto block_align = static_cast<int>(unit.bytes.size());
    impl.audio_decoder = atrac3p_alloc(unit.channels, &block_align);
    impl.audio_block_align = unit.bytes.size();
    impl.audio_channels = unit.channels;
  }
  if (impl.audio_decoder == nullptr) return false;
  std::array<float, 4096> left{};
  std::array<float, 4096> right{};
  float* planes[]{left.data(), right.data()};
  int decoded{};
  const auto result = atrac3p_decode_frame(
      impl.audio_decoder, planes, &decoded, unit.bytes.data(),
      static_cast<int>(unit.bytes.size()));
  if (result < 0 || decoded < 0 || decoded > 2048) return false;
  const auto to_sample = [](float value) {
    if (!std::isfinite(value)) return static_cast<std::int16_t>(0);
    value = std::clamp(value, -1.0F, 1.0F);
    return static_cast<std::int16_t>(value * 32767.0F);
  };
  for (int index = 0; index < decoded; ++index) {
    output[static_cast<std::size_t>(index) * 2U] =
        to_sample(left[static_cast<std::size_t>(index)]);
    output[static_cast<std::size_t>(index) * 2U + 1U] =
        to_sample(unit.channels == 1U ? left[static_cast<std::size_t>(index)]
                                     : right[static_cast<std::size_t>(index)]);
  }
  sample_count = static_cast<std::uint32_t>(decoded);
  impl.pending_audio.reset();
  return true;
}

bool MediaEngine::drain_video(std::span<std::uint8_t> output,
                              std::uint32_t frame_stride,
                              std::uint32_t pixel_format,
                              VideoFrameInfo& info) {
  info = {};
#if defined(REFRACT_HAS_FFMPEG)
  auto& impl = *implementation_;
  std::lock_guard lock(impl.mutex);
  if (!impl.ensure_decoder()) return false;
  const auto send_result = avcodec_send_packet(impl.codec, nullptr);
  if (send_result < 0 && send_result != AVERROR_EOF) return false;
  const auto receive_result = avcodec_receive_frame(impl.codec, impl.frame);
  if (receive_result == AVERROR(EAGAIN) || receive_result == AVERROR_EOF)
    return true;
  if (receive_result < 0) return false;
  return copy_video_frame(impl.frame, impl.scaler, impl.rgba_scratch, output,
                          frame_stride, pixel_format, impl.frame_number,
                          impl.last_frame, info);
#else
  static_cast<void>(output);
  static_cast<void>(frame_stride);
  static_cast<void>(pixel_format);
  return false;
#endif
}

void MediaEngine::flush() {
  auto& impl = *implementation_;
  std::lock_guard lock(impl.mutex);
  impl.encoded.clear();
  impl.encoded_offset = 0U;
  impl.video_bytes.clear();
  impl.audio_staging.clear();
  impl.units.clear();
  impl.pending_video.reset();
  impl.pending_audio.reset();
  impl.video_next_pts = -1;
  impl.last_video_pts = -1;
  impl.last_audio_pts = -1;
  if (impl.audio_decoder != nullptr) atrac3p_flush_buffers(impl.audio_decoder);
  impl.frame_number = 0U;
  impl.last_frame = {};
#if defined(REFRACT_HAS_FFMPEG)
  if (impl.codec != nullptr) avcodec_flush_buffers(impl.codec);
#endif
}

std::size_t MediaEngine::buffered_bytes() const {
  const auto& impl = *implementation_;
  std::lock_guard lock(impl.mutex);
  std::size_t result = (impl.encoded.size() - impl.encoded_offset) +
                       impl.video_bytes.size() + impl.audio_staging_size();
  for (const auto& item : impl.units) result += item.second.source_bytes;
  return result;
}

std::size_t MediaEngine::packets_in_use(std::size_t packet_size) const {
  if (packet_size == 0U) return 0U;
  const auto bytes = buffered_bytes();
  return (bytes + packet_size - 1U) / packet_size;
}

bool MediaEngine::ffmpeg_available() const {
#if defined(REFRACT_HAS_FFMPEG)
  return true;
#else
  return false;
#endif
}

std::shared_ptr<MediaEngine> create_media_engine(
    std::uint32_t mpeg_address, std::size_t capacity_bytes) {
  auto engine = std::make_shared<MediaEngine>(capacity_bytes);
  std::lock_guard lock(engines_mutex());
  engines()[mpeg_address] = engine;
  return engine;
}

std::shared_ptr<MediaEngine> find_media_engine(std::uint32_t mpeg_address) {
  std::lock_guard lock(engines_mutex());
  const auto found = engines().find(mpeg_address);
  return found == engines().end() ? nullptr : found->second;
}

void delete_media_engine(std::uint32_t mpeg_address) {
  std::lock_guard lock(engines_mutex());
  engines().erase(mpeg_address);
}

void reset_media_engines() {
  std::lock_guard lock(engines_mutex());
  engines().clear();
  ringbuffer_gps().clear();
}

void remember_ringbuffer_gp(std::uint32_t ringbuffer_address,
                            std::uint32_t gp) {
  std::lock_guard lock(engines_mutex());
  ringbuffer_gps()[ringbuffer_address] = gp;
}

std::uint32_t ringbuffer_gp(std::uint32_t ringbuffer_address) {
  std::lock_guard lock(engines_mutex());
  const auto found = ringbuffer_gps().find(ringbuffer_address);
  return found == ringbuffer_gps().end() ? 0U : found->second;
}

void forget_ringbuffer_gp(std::uint32_t ringbuffer_address) {
  std::lock_guard lock(engines_mutex());
  ringbuffer_gps().erase(ringbuffer_address);
}

} // namespace mpeg_state
