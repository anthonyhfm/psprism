#pragma once

#if !defined(__PSP__)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "../../../third_party/at3_standalone/at3_decoders.h"

namespace atrac_state {

constexpr std::uint32_t success = 0U;
constexpr std::uint32_t invalid_id = 0x80630005U;
constexpr std::uint32_t bad_codec_type = 0x80630004U;
constexpr std::uint32_t unknown_format = 0x80630006U;
constexpr std::uint32_t unmatched_format = 0x80630007U;
constexpr std::uint32_t bad_data = 0x80630008U;
constexpr std::uint32_t unset_data = 0x80630010U;
constexpr std::uint32_t no_data = 0x80630023U;
constexpr std::uint32_t all_data_decoded = 0x80630024U;
constexpr std::uint32_t second_buffer_not_needed = 0x80630022U;
constexpr std::uint32_t no_loop_information = 0x80630021U;
constexpr std::uint32_t codec_atrac3plus = 0x1000U;
constexpr std::uint32_t codec_atrac3 = 0x1001U;
constexpr std::uint32_t all_data_on_memory = 0xffffffffU;

struct Track {
  std::uint32_t codec_type{};
  std::uint32_t channels{2U};
  std::uint32_t sample_rate{44100U};
  std::uint32_t bitrate{};
  std::uint32_t block_align{};
  std::uint32_t file_size{};
  std::uint32_t data_offset{};
  std::uint32_t data_size{};
  std::uint32_t end_sample{};
  std::uint32_t first_sample_offset{};
  std::uint32_t loop_start{all_data_on_memory};
  std::uint32_t loop_end{all_data_on_memory};
  std::uint32_t loop_play_count{};
  std::uint16_t joint_stereo{};

  [[nodiscard]] std::uint32_t samples_per_frame() const {
    return codec_type == codec_atrac3plus ? 2048U : 1024U;
  }

  [[nodiscard]] std::uint32_t decoder_delay() const {
    return codec_type == codec_atrac3plus ? 0x170U : 0x45U;
  }

  [[nodiscard]] std::uint32_t initial_skip_samples() const {
    return first_sample_offset + decoder_delay();
  }
};

inline bool read_u16(const std::uint8_t* data, std::size_t size,
                     std::size_t offset, std::uint16_t& value) {
  if (offset > size || size - offset < sizeof(value)) return false;
  std::memcpy(&value, data + offset, sizeof(value));
  return true;
}

inline bool read_u32(const std::uint8_t* data, std::size_t size,
                     std::size_t offset, std::uint32_t& value) {
  if (offset > size || size - offset < sizeof(value)) return false;
  std::memcpy(&value, data + offset, sizeof(value));
  return true;
}

inline bool parse_riff(const std::uint8_t* data, std::size_t size,
                       Track& track) {
  constexpr std::uint32_t riff = 0x46464952U;
  constexpr std::uint32_t wave = 0x45564157U;
  constexpr std::uint32_t fmt = 0x20746d66U;
  constexpr std::uint32_t fact = 0x74636166U;
  constexpr std::uint32_t smpl = 0x6c706d73U;
  constexpr std::uint32_t data_chunk = 0x61746164U;
  constexpr std::uint16_t wave_atrac3 = 0x0270U;
  constexpr std::uint16_t wave_extensible = 0xfffeU;

  track = {};
  track.loop_start = all_data_on_memory;
  track.loop_end = all_data_on_memory;
  std::uint32_t magic{};
  std::uint32_t riff_size{};
  std::uint32_t wave_magic{};
  if (!read_u32(data, size, 0U, magic) || magic != riff ||
      !read_u32(data, size, 4U, riff_size) ||
      !read_u32(data, size, 8U, wave_magic) || wave_magic != wave)
    return false;

  track.file_size = riff_size > UINT32_MAX - 8U ? UINT32_MAX : riff_size + 8U;
  bool found_format = false;
  bool found_data = false;
  std::size_t offset = 12U;
  while (offset <= size && size - offset >= 8U) {
    std::uint32_t chunk_id{};
    std::uint32_t chunk_size{};
    read_u32(data, size, offset, chunk_id);
    read_u32(data, size, offset + 4U, chunk_size);
    const auto payload = offset + 8U;

    if (chunk_id == fmt) {
      std::uint16_t tag{};
      std::uint16_t channels{};
      std::uint16_t block_align{};
      std::uint32_t sample_rate{};
      std::uint32_t average_bytes{};
      if (chunk_size < 16U || !read_u16(data, size, payload, tag) ||
          !read_u16(data, size, payload + 2U, channels) ||
          !read_u32(data, size, payload + 4U, sample_rate) ||
          !read_u32(data, size, payload + 8U, average_bytes) ||
          !read_u16(data, size, payload + 12U, block_align))
        return false;
      if ((tag != wave_atrac3 && tag != wave_extensible) ||
          (channels != 1U && channels != 2U) || sample_rate != 44100U ||
          block_align == 0U)
        return false;
      track.codec_type = tag == wave_extensible ? codec_atrac3plus
                                                 : codec_atrac3;
      track.channels = channels;
      track.sample_rate = sample_rate;
      track.bitrate = average_bytes * 8U;
      track.block_align = block_align;
      if (tag == wave_atrac3 && chunk_size >= 26U)
        read_u16(data, size, payload + 24U, track.joint_stereo);
      found_format = true;
    } else if (chunk_id == fact) {
      read_u32(data, size, payload, track.end_sample);
      if (chunk_size >= 8U)
        read_u32(data, size, payload + 4U, track.first_sample_offset);
    } else if (chunk_id == smpl && chunk_size >= 60U) {
      std::uint32_t loop_count{};
      read_u32(data, size, payload + 28U, loop_count);
      if (loop_count != 0U) {
        if (!read_u32(data, size, payload + 44U, track.loop_start) ||
            !read_u32(data, size, payload + 48U, track.loop_end) ||
            !read_u32(data, size, payload + 56U, track.loop_play_count))
          return false;
      }
    } else if (chunk_id == data_chunk) {
      track.data_offset = static_cast<std::uint32_t>(payload);
      track.data_size = chunk_size;
      found_data = true;
      break;
    }

    const auto padded_size = static_cast<std::uint64_t>(chunk_size) +
                             (chunk_size & 1U);
    const auto next = static_cast<std::uint64_t>(payload) + padded_size;
    if (next > size) return false;
    offset = static_cast<std::size_t>(next);
  }

  if (!found_format || !found_data) return false;
  if (track.end_sample == 0U && track.block_align != 0U) {
    const auto frames = track.data_size / track.block_align;
    const auto total_samples = static_cast<std::uint64_t>(frames) *
                               track.samples_per_frame();
    const auto playable_samples = total_samples > track.first_sample_offset
                                      ? total_samples -
                                            track.first_sample_offset
                                      : 0U;
    track.end_sample = playable_samples == 0U
                           ? 0U
                           : static_cast<std::uint32_t>(playable_samples - 1U);
  } else if (track.end_sample != 0U) {
    // The RIFF fact value is a sample count, while the PSP APIs expose the
    // inclusive index of the final playable sample.
    --track.end_sample;
  }
  if (track.loop_start != all_data_on_memory &&
      (track.loop_start > track.loop_end ||
       track.loop_end > track.end_sample))
    return false;
  if (track.loop_start != all_data_on_memory) {
    if (track.loop_start < track.first_sample_offset ||
        track.loop_end < track.first_sample_offset)
      return false;
    track.loop_start -= track.first_sample_offset;
    track.loop_end -= track.first_sample_offset;
  }
  return true;
}

struct Decoder {
  explicit Decoder(std::uint32_t requested_codec = 0U)
      : requested_codec_type(requested_codec) {}

  ~Decoder() {
    if (atrac3plus != nullptr) atrac3p_free(atrac3plus);
    if (atrac3 != nullptr) atrac3_free(atrac3);
  }

  Decoder(const Decoder&) = delete;
  Decoder& operator=(const Decoder&) = delete;

  bool set_data(const std::uint8_t* source, std::size_t size,
                std::uint32_t guest_address) {
    Track parsed;
    if (source == nullptr || !parse_riff(source, size, parsed)) return false;
    if (requested_codec_type != 0U &&
        requested_codec_type != parsed.codec_type)
      return false;

    if (atrac3plus != nullptr) atrac3p_free(atrac3plus);
    if (atrac3 != nullptr) atrac3_free(atrac3);
    atrac3plus = nullptr;
    atrac3 = nullptr;

    requested_codec_type = parsed.codec_type;
    track = parsed;
    guest_buffer = guest_address;
    guest_buffer_size = static_cast<std::uint32_t>(size);
    encoded.assign(source, source + size);
    data_cursor = track.data_offset;
    decoded_samples = 0U;
    skip_samples = track.initial_skip_samples();
    finished = false;
    internal_error = 0U;

    auto block_align = static_cast<int>(track.block_align);
    if (track.codec_type == codec_atrac3plus) {
      atrac3plus = atrac3p_alloc(static_cast<int>(track.channels),
                                 &block_align);
    } else {
      std::array<std::uint8_t, 14> extra_data{};
      extra_data[0] = 1U;
      extra_data[3] = static_cast<std::uint8_t>(track.channels << 3U);
      extra_data[6] = static_cast<std::uint8_t>(track.joint_stereo);
      extra_data[8] = static_cast<std::uint8_t>(track.joint_stereo);
      extra_data[10] = 1U;
      atrac3 = atrac3_alloc(static_cast<int>(track.channels), &block_align,
                            extra_data.data(),
                            static_cast<int>(extra_data.size()));
    }
    return atrac3plus != nullptr || atrac3 != nullptr;
  }

  void rewind() {
    data_cursor = track.data_offset;
    decoded_samples = 0U;
    skip_samples = track.initial_skip_samples();
    finished = false;
    if (atrac3plus != nullptr) atrac3p_flush_buffers(atrac3plus);
    if (atrac3 != nullptr) atrac3_flush_buffers(atrac3);
  }

  bool decode_preroll_packet(std::uint32_t offset) {
    if (static_cast<std::uint64_t>(offset) + track.block_align >
        encoded.size())
      return false;
    std::array<float, 4096> left{};
    std::array<float, 4096> right{};
    float* planes[2]{left.data(), right.data()};
    int decoded_count{};
    const auto* packet = encoded.data() + offset;
    const auto result = atrac3plus != nullptr
                            ? atrac3p_decode_frame(
                                  atrac3plus, planes, &decoded_count, packet,
                                  static_cast<int>(track.block_align))
                            : atrac3 != nullptr
                                  ? atrac3_decode_frame(
                                        atrac3, planes, &decoded_count, packet,
                                        static_cast<int>(track.block_align))
                                  : -1;
    return result >= 0;
  }

  bool seek(std::uint32_t sample) {
    if (track.block_align == 0U || sample > track.end_sample) return false;
    const auto absolute_sample =
        static_cast<std::uint64_t>(track.initial_skip_samples()) + sample;
    const auto frame = absolute_sample / track.samples_per_frame();
    const auto offset = static_cast<std::uint64_t>(track.data_offset) +
                        frame * track.block_align;
    if (offset >= encoded.size()) return false;
    if (atrac3plus != nullptr) atrac3p_flush_buffers(atrac3plus);
    if (atrac3 != nullptr) atrac3_flush_buffers(atrac3);
    // ATRAC frames overlap.  Re-prime decoder history with up to two packets
    // immediately preceding the target packet before exposing target PCM.
    const auto first_preroll_frame = frame > 2U ? frame - 2U : 0U;
    for (auto preroll_frame = first_preroll_frame; preroll_frame < frame;
         ++preroll_frame) {
      const auto preroll_offset =
          static_cast<std::uint64_t>(track.data_offset) +
          preroll_frame * track.block_align;
      if (preroll_offset > UINT32_MAX ||
          !decode_preroll_packet(static_cast<std::uint32_t>(preroll_offset)))
        return false;
    }
    data_cursor = static_cast<std::uint32_t>(offset);
    decoded_samples = sample;
    skip_samples =
        static_cast<std::uint32_t>(absolute_sample % track.samples_per_frame());
    finished = false;
    return true;
  }

  [[nodiscard]] bool loop_active() const {
    return loop_count != 0 && track.loop_start != all_data_on_memory &&
           track.loop_end != all_data_on_memory &&
           track.loop_start <= track.loop_end;
  }

  [[nodiscard]] std::uint32_t limit_output_samples(
      std::uint32_t available) const {
    if (!loop_active() || decoded_samples > track.loop_end) return available;
    const auto through_loop_end = static_cast<std::uint64_t>(track.loop_end) +
                                  1U - decoded_samples;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(
        available, through_loop_end));
  }

  bool advance_output_position(std::uint32_t sample_count) {
    decoded_samples += sample_count;
    if (!loop_active() || decoded_samples <= track.loop_end) return false;
    loop_pending = true;
    if (!seek(track.loop_start)) return false;
    loop_pending = false;
    if (loop_count > 0) --loop_count;
    return true;
  }

  std::uint32_t decode(std::int16_t* output, std::uint32_t& sample_count,
                       bool& reached_end) {
    sample_count = 0U;
    reached_end = false;
    if (encoded.empty() || track.block_align == 0U) return unset_data;
    if (loop_pending) {
      if (!seek(track.loop_start)) return no_data;
      loop_pending = false;
      if (loop_count > 0) --loop_count;
    }
    const auto end_exclusive = static_cast<std::uint64_t>(track.end_sample) + 1U;
    if (decoded_samples >= end_exclusive) {
      if (loop_count != 0) {
        if (loop_count > 0) --loop_count;
        rewind();
      } else {
        finished = true;
        reached_end = true;
        return all_data_decoded;
      }
    }

    const auto declared_end = static_cast<std::uint64_t>(track.data_offset) +
                              track.data_size;
    const auto available_end = std::min<std::uint64_t>(declared_end,
                                                       encoded.size());
    if (static_cast<std::uint64_t>(data_cursor) + track.block_align >
        available_end) {
      if (available_end < declared_end) return no_data;
      if (loop_count != 0) {
        if (loop_count > 0) --loop_count;
        rewind();
      } else {
        finished = true;
        reached_end = true;
        return all_data_decoded;
      }
    }
    if (static_cast<std::uint64_t>(data_cursor) + track.block_align >
        encoded.size())
      return no_data;

    std::array<float, 4096> left{};
    std::array<float, 4096> right{};
    float* planes[2]{left.data(), right.data()};
    int decoded_count{};
    int result{};
    const auto* packet = encoded.data() + data_cursor;
    if (atrac3plus != nullptr) {
      result = atrac3p_decode_frame(atrac3plus, planes, &decoded_count, packet,
                                    static_cast<int>(track.block_align));
    } else if (atrac3 != nullptr) {
      result = atrac3_decode_frame(atrac3, planes, &decoded_count, packet,
                                   static_cast<int>(track.block_align));
    } else {
      return bad_data;
    }

    data_cursor += track.block_align;
    if (result < 0 || decoded_count < 0 || decoded_count > 4096) {
      internal_error = static_cast<std::uint32_t>(result);
      return bad_data;
    }

    const auto skipped = std::min(skip_samples,
                                  static_cast<std::uint32_t>(decoded_count));
    skip_samples -= skipped;
    const auto decoded_after_skip =
        static_cast<std::uint32_t>(decoded_count) - skipped;
    const auto remaining = static_cast<std::uint32_t>(
        end_exclusive > decoded_samples
            ? end_exclusive - decoded_samples
            : 0U);
    sample_count = limit_output_samples(
        std::min(decoded_after_skip, remaining));
    last_peak = 0.0F;
    if (output != nullptr) {
      for (std::uint32_t index = 0; index < sample_count; ++index) {
        const auto clamp = [](float value) -> std::int16_t {
          if (!std::isfinite(value)) return 0;
          value = std::clamp(value, -1.0F, 1.0F);
          return static_cast<std::int16_t>(value * 32767.0F);
        };
        const auto source_index = static_cast<std::size_t>(skipped + index);
        const auto left_sample = left[source_index];
        const auto right_sample =
            track.channels == 2U ? right[source_index]
                                 : left_sample;
        last_peak = std::max(
            last_peak, std::max(std::abs(left_sample), std::abs(right_sample)));
        output[index * 2U] = clamp(left_sample);
        output[index * 2U + 1U] = clamp(right_sample);
      }
    }
    const auto looped = advance_output_position(sample_count);
    ++decoded_frames;
    reached_end = !looped && decoded_samples >= end_exclusive &&
                  loop_count == 0;
    finished = reached_end;
    return success;
  }

  [[nodiscard]] std::uint32_t next_samples() const {
    if (finished) return 0U;
    const auto frame_samples = track.samples_per_frame() > skip_samples
                                   ? track.samples_per_frame() - skip_samples
                                   : 0U;
    const auto end_exclusive = static_cast<std::uint64_t>(track.end_sample) + 1U;
    const auto remaining = static_cast<std::uint32_t>(
        end_exclusive > decoded_samples
            ? end_exclusive - decoded_samples
            : 0U);
    return limit_output_samples(std::min(frame_samples, remaining));
  }

  [[nodiscard]] std::uint32_t remaining_frames() const {
    const auto declared_end = static_cast<std::uint64_t>(track.data_offset) +
                              track.data_size;
    if (encoded.size() >= declared_end) return all_data_on_memory;
    if (data_cursor >= encoded.size() || track.block_align == 0U) return 0U;
    return static_cast<std::uint32_t>((encoded.size() - data_cursor) /
                                      track.block_align);
  }

  void stream_data_info(std::uint32_t& write_pointer,
                        std::uint32_t& writable_bytes,
                        std::uint32_t& read_offset) {
    read_offset = static_cast<std::uint32_t>(encoded.size());
    write_pointer = guest_buffer;
    writable_bytes = 0U;
    pending_write_pointer = guest_buffer;
    pending_writable_bytes = 0U;
    pending_read_offset = read_offset;
    if (guest_buffer_size == 0U || encoded.size() >= track.file_size) return;

    const auto retained_bytes = encoded.size() > data_cursor
                                    ? encoded.size() - data_cursor
                                    : 0U;
    if (retained_bytes >= guest_buffer_size) return;
    const auto ring_offset = static_cast<std::uint32_t>(
        encoded.size() % static_cast<std::size_t>(guest_buffer_size));
    const auto free_bytes = static_cast<std::size_t>(guest_buffer_size) -
                            retained_bytes;
    const auto contiguous_bytes =
        static_cast<std::size_t>(guest_buffer_size - ring_offset);
    const auto file_bytes = static_cast<std::size_t>(track.file_size) -
                            encoded.size();
    writable_bytes = static_cast<std::uint32_t>(
        std::min({free_bytes, contiguous_bytes, file_bytes}));
    write_pointer = guest_buffer + ring_offset;
    pending_write_pointer = write_pointer;
    pending_writable_bytes = writable_bytes;
  }

  std::uint32_t add_stream_data(psprecomp::State& state,
                                std::uint32_t byte_count) {
    if (byte_count > pending_writable_bytes) return bad_data;
    if (byte_count == 0U) return success;
    const auto* source = psprecomp::mapped_address(
        state, pending_write_pointer, byte_count);
    if (source == nullptr) return bad_data;
    encoded.insert(encoded.end(), source, source + byte_count);
    pending_write_pointer += byte_count;
    pending_writable_bytes -= byte_count;
    pending_read_offset += byte_count;
    return success;
  }

  std::mutex decoder_mutex;
  std::uint32_t requested_codec_type{};
  Track track{};
  std::uint32_t guest_buffer{};
  std::uint32_t guest_buffer_size{};
  std::vector<std::uint8_t> encoded;
  std::uint32_t data_cursor{};
  std::uint32_t pending_write_pointer{};
  std::uint32_t pending_writable_bytes{};
  std::uint32_t pending_read_offset{};
  std::uint64_t decoded_samples{};
  std::uint64_t decoded_frames{};
  std::uint32_t skip_samples{};
  int loop_count{};
  bool finished{};
  bool loop_pending{};
  bool audible_reported{};
  float last_peak{};
  std::uint32_t internal_error{};
  ATRAC3PContext* atrac3plus{};
  ATRAC3Context* atrac3{};
};

inline std::mutex& mutex() {
  static std::mutex value;
  return value;
}

inline std::unordered_map<int, std::shared_ptr<Decoder>>& decoders() {
  static std::unordered_map<int, std::shared_ptr<Decoder>> value;
  return value;
}

inline int create(std::uint32_t codec_type = 0U) {
  static int next_id = 0;
  if (codec_type != 0U && codec_type != codec_atrac3 &&
      codec_type != codec_atrac3plus)
    return static_cast<int>(bad_codec_type);
  std::lock_guard lock(mutex());
  const auto id = next_id++;
  decoders()[id] = std::make_shared<Decoder>(codec_type);
  return id;
}

inline std::shared_ptr<Decoder> get(int id) {
  std::lock_guard lock(mutex());
  const auto found = decoders().find(id);
  return found == decoders().end() ? nullptr : found->second;
}

inline bool release(int id) {
  std::lock_guard lock(mutex());
  return decoders().erase(id) != 0U;
}

inline bool write_u32(psprecomp::State& state, std::uint32_t address,
                      std::uint32_t value) {
  auto* output = psprecomp::mapped_address(state, address, sizeof(value));
  if (output == nullptr) return false;
  std::memcpy(output, &value, sizeof(value));
  return true;
}

inline std::uint32_t read_guest_u32(psprecomp::State& state,
                                    std::uint32_t address) {
  std::uint32_t value{};
  if (const auto* input =
          psprecomp::mapped_address(state, address, sizeof(value)))
    std::memcpy(&value, input, sizeof(value));
  return value;
}

inline std::uint32_t set_data(psprecomp::State& state, int id,
                              std::uint32_t address, std::uint32_t size) {
  const auto decoder = get(id);
  if (decoder == nullptr) return invalid_id;
  const auto* source = psprecomp::mapped_address(state, address, size);
  if (source == nullptr) return bad_data;
  std::lock_guard lock(decoder->decoder_mutex);
  Track parsed;
  if (!parse_riff(source, size, parsed)) return unknown_format;
  if (decoder->requested_codec_type != 0U &&
      decoder->requested_codec_type != parsed.codec_type)
    return unmatched_format;
  return decoder->set_data(source, size, address) ? success : bad_data;
}

} // namespace atrac_state

#endif
