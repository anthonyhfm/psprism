#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace mpeg_state {

enum class StreamKind : std::uint8_t { video, audio, other };

struct PsmfHeader {
  std::uint32_t stream_offset{};
  std::uint32_t stream_size{};
  std::int64_t first_timestamp{};
  std::int64_t last_timestamp{};
  std::uint32_t width{};
  std::uint32_t height{};
};

struct MediaAccessUnit {
  std::vector<std::uint8_t> bytes;
  std::int64_t pts{-1};
  std::int64_t dts{-1};
  std::uint32_t source_bytes{};
  std::uint8_t channel{};
  std::uint8_t channels{2U};
};

struct VideoFrameInfo {
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t frame_number{};
  bool produced{};
};

std::optional<PsmfHeader> parse_psmf_header(std::span<const std::uint8_t> data);

class MediaEngine {
 public:
  explicit MediaEngine(std::size_t capacity_bytes);
  ~MediaEngine();
  MediaEngine(const MediaEngine&) = delete;
  MediaEngine& operator=(const MediaEngine&) = delete;

  bool append_packets(std::span<const std::uint8_t> bytes);
  std::uint32_t register_stream(std::uint32_t type, std::uint32_t channel);
  bool unregister_stream(std::uint32_t uid);
  std::optional<MediaAccessUnit> next_access_unit(std::uint32_t uid);
  std::optional<StreamKind> stream_kind(std::uint32_t uid) const;
  bool set_video_mode(std::uint32_t pixel_format);
  std::uint32_t video_pixel_format() const;
  VideoFrameInfo last_video_frame() const;

  bool decode_video(const MediaAccessUnit& access_unit,
                    std::span<std::uint8_t> output,
                    std::uint32_t frame_stride, std::uint32_t pixel_format,
                    VideoFrameInfo& info);
  bool decode_pending_video(std::span<std::uint8_t> output,
                            std::uint32_t frame_stride,
                            std::uint32_t pixel_format, VideoFrameInfo& info);
  std::optional<MediaAccessUnit> take_pending_audio();
  bool decode_pending_audio(std::span<std::int16_t> output,
                            std::uint32_t& sample_count);
  bool drain_video(std::span<std::uint8_t> output,
                   std::uint32_t frame_stride, std::uint32_t pixel_format,
                   VideoFrameInfo& info);

  void flush();
  std::size_t buffered_bytes() const;
  std::size_t packets_in_use(std::size_t packet_size) const;
  bool ffmpeg_available() const;

 private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

std::shared_ptr<MediaEngine> create_media_engine(std::uint32_t mpeg_address,
                                                 std::size_t capacity_bytes);
std::shared_ptr<MediaEngine> find_media_engine(std::uint32_t mpeg_address);
void delete_media_engine(std::uint32_t mpeg_address);
void reset_media_engines();

} // namespace mpeg_state
