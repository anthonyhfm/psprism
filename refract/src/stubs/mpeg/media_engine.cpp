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
constexpr std::size_t maximum_access_units = 64U;

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

  void queue_packet(std::size_t begin, std::size_t end, std::uint8_t code) {
    const bool video = code >= 0xe0U && code <= 0xefU;
    const bool audio = code == 0xbdU;
    if ((!video && !audio) || end <= begin || units.size() >= maximum_access_units)
      return;
    const auto packet = std::span<const std::uint8_t>(encoded).subspan(
        begin, end - begin);
    const auto header = parse_pes_header(packet, audio);
    if (!header || header->payload_offset >= packet.size()) return;
    const auto payload = packet.subspan(header->payload_offset);
    if (audio) {
      if (audio_bytes.empty() && header->pts >= 0) audio_next_pts = header->pts;
      audio_bytes.insert(audio_bytes.end(), payload.begin(), payload.end());
      extract_audio_frames(header->channel);
      return;
    }
    MediaAccessUnit unit;
    unit.bytes.assign(payload.begin(), payload.end());
    unit.pts = header->pts;
    unit.dts = header->dts;
    unit.source_bytes = static_cast<std::uint32_t>(packet.size());
    units.push_back({StreamKind::video, std::move(unit)});
  }

  void extract_audio_frames(std::uint8_t channel) {
    constexpr std::array<std::uint8_t, 2> audio_header{0x0fU, 0xd0U};
    for (;;) {
      const auto header = std::search(audio_bytes.begin(), audio_bytes.end(),
                                      audio_header.begin(), audio_header.end());
      if (header == audio_bytes.end()) {
        if (audio_bytes.size() > 1U) {
          const auto last = audio_bytes.back();
          audio_bytes.clear();
          if (last == 0x0fU) audio_bytes.push_back(last);
        }
        return;
      }
      if (header != audio_bytes.begin()) audio_bytes.erase(audio_bytes.begin(), header);
      if (audio_bytes.size() < 4U) return;
      const auto code1 = audio_bytes[2];
      const auto code2 = audio_bytes[3];
      const auto frame_size =
          static_cast<std::size_t>(((code1 & 3U) << 8U) | (code2 * 8U)) + 16U;
      if (frame_size < 8U || frame_size > 8192U) {
        audio_bytes.erase(audio_bytes.begin());
        continue;
      }
      if (audio_bytes.size() < frame_size) return;
      MediaAccessUnit unit;
      unit.bytes.assign(audio_bytes.begin() + 8, audio_bytes.begin() +
                                                      static_cast<std::ptrdiff_t>(frame_size));
      unit.pts = audio_next_pts;
      unit.dts = audio_next_pts;
      unit.source_bytes = static_cast<std::uint32_t>(frame_size);
      unit.channel = channel;
      unit.channels = code1 == 0x24U ? 1U : 2U;
      units.push_back({StreamKind::audio, std::move(unit)});
      if (audio_next_pts >= 0) audio_next_pts += 4180;
      audio_bytes.erase(audio_bytes.begin(),
                        audio_bytes.begin() + static_cast<std::ptrdiff_t>(frame_size));
      if (units.size() >= maximum_access_units) return;
    }
  }

  void demux() {
    std::size_t cursor{};
    std::size_t consumed{};
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
        queue_packet(cursor, next, code);
        cursor = next;
        consumed = cursor;
        continue;
      }
      const auto end = cursor + 6U + payload_size;
      if (end > encoded.size()) break;
      queue_packet(cursor, end, code);
      cursor = end;
      consumed = cursor;
    }
    if (consumed != 0U) {
      encoded.erase(encoded.begin(),
                    encoded.begin() + static_cast<std::ptrdiff_t>(consumed));
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
    parser = av_parser_init(AV_CODEC_ID_H264);
    if (codec == nullptr || packet == nullptr || frame == nullptr ||
        parser == nullptr || avcodec_open2(codec, decoder, nullptr) < 0) {
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
  std::vector<std::uint8_t> audio_bytes;
  std::deque<std::pair<StreamKind, MediaAccessUnit>> units;
  std::unordered_map<std::uint32_t, Stream> streams;
  std::uint32_t next_stream{1U};
  std::size_t queued_payload_bytes{};
  std::uint32_t frame_number{};
  std::uint32_t pixel_format{3U};
  VideoFrameInfo last_frame{};
  std::optional<MediaAccessUnit> pending_video;
  std::optional<MediaAccessUnit> pending_audio;
  std::int64_t audio_next_pts{-1};
  ATRAC3PContext* audio_decoder{};
  std::size_t audio_block_align{};
  std::uint8_t audio_channels{};
#if defined(REFRACT_HAS_FFMPEG)
  AVCodecContext* codec{};
  AVCodecParserContext* parser{};
  AVPacket* packet{};
  AVFrame* frame{};
  SwsContext* scaler{};
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
  if (bytes.size() > impl.capacity ||
      impl.encoded.size() + impl.audio_bytes.size() + queued >
          impl.capacity - bytes.size()) {
    return false;
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

bool MediaEngine::set_video_mode(std::uint32_t pixel_format) {
  if (pixel_format > 3U) return false;
  auto& impl = *implementation_;
  std::lock_guard lock(impl.mutex);
  impl.pixel_format = pixel_format;
  return true;
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
                stream->second.channel == 0U ||
                candidate.second.channel == 0U ||
                (candidate.second.channel & 0x0fU) == stream->second.channel);
      });
  if (found == impl.units.end()) return std::nullopt;
  auto result = std::move(found->second);
  impl.units.erase(found);
  if (stream->second.kind == StreamKind::video) {
    impl.pending_video = result;
  } else if (stream->second.kind == StreamKind::audio) {
    impl.pending_audio = result;
  }
  return result;
}

namespace {

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
  const std::uint8_t* input = access_unit.bytes.data();
  auto remaining = static_cast<int>(std::min<std::size_t>(
      access_unit.bytes.size(), static_cast<std::size_t>(std::numeric_limits<int>::max())));
  while (remaining > 0) {
    std::uint8_t* parsed{};
    int parsed_size{};
    const auto consumed = av_parser_parse2(
        impl.parser, impl.codec, &parsed, &parsed_size, input, remaining,
        access_unit.pts, access_unit.dts, 0);
    if (consumed < 0) return false;
    input += consumed;
    remaining -= consumed;
    if (parsed_size == 0) {
      if (consumed == 0) break;
      continue;
    }
    impl.packet->data = parsed;
    impl.packet->size = parsed_size;
    impl.packet->pts = access_unit.pts;
    impl.packet->dts = access_unit.dts;
    if (avcodec_send_packet(impl.codec, impl.packet) < 0) return false;
    if (avcodec_receive_frame(impl.codec, impl.frame) != 0) continue;
    const auto width = static_cast<std::uint32_t>(impl.frame->width);
    const auto height = static_cast<std::uint32_t>(impl.frame->height);
    if (width == 0U || height == 0U || frame_stride < width) return false;
    const auto bytes_per_pixel = pixel_format == 3U ? 4U : 2U;
    if (height > output.size() / frame_stride / bytes_per_pixel) return false;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4U);
    impl.scaler = sws_getCachedContext(
        impl.scaler, impl.frame->width, impl.frame->height,
        static_cast<AVPixelFormat>(impl.frame->format), impl.frame->width,
        impl.frame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr,
        nullptr);
    if (impl.scaler == nullptr) return false;
    std::uint8_t* destination[] = {rgba.data()};
    const int destination_stride[] = {impl.frame->width * 4};
    sws_scale(impl.scaler, impl.frame->data, impl.frame->linesize, 0,
              impl.frame->height, destination, destination_stride);
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
    info = {width, height, ++impl.frame_number, true};
    impl.last_frame = info;
    return true;
  }
  return true;
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
  static_cast<void>(output);
  static_cast<void>(frame_stride);
  static_cast<void>(pixel_format);
  info = {};
#if defined(REFRACT_HAS_FFMPEG)
  auto& impl = *implementation_;
  std::lock_guard lock(impl.mutex);
  if (!impl.ensure_decoder() || avcodec_send_packet(impl.codec, nullptr) < 0)
    return false;
  return avcodec_receive_frame(impl.codec, impl.frame) == 0;
#else
  return false;
#endif
}

void MediaEngine::flush() {
  auto& impl = *implementation_;
  std::lock_guard lock(impl.mutex);
  impl.encoded.clear();
  impl.audio_bytes.clear();
  impl.units.clear();
  impl.pending_video.reset();
  impl.pending_audio.reset();
  impl.audio_next_pts = -1;
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
  std::size_t result = impl.encoded.size() + impl.audio_bytes.size();
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
}

} // namespace mpeg_state
