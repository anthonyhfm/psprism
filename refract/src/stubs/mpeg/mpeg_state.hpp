#pragma once

#include <psprecomp/runtime.hpp>

#include "media_engine.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mpeg_state {

inline constexpr std::uint32_t avc_es_size = 2048U;
inline constexpr std::uint32_t atrac_es_size = 2112U;
inline constexpr std::uint32_t atrac_es_output_size = 8192U;
inline constexpr std::uint32_t required_memory_size = 0xb3dbU;
inline constexpr std::uint32_t ringbuffer_packet_size = 2048U;
inline constexpr std::uint32_t ringbuffer_packet_overhead = 104U;
inline constexpr std::uint32_t no_data = 0x80618001U;
inline constexpr std::uint32_t bad_version = 0x80610002U;
inline constexpr std::uint32_t invalid_address = 0x80610103U;
inline constexpr std::uint32_t invalid_value = 0x806101feU;
inline constexpr std::uint32_t insufficient_memory = 0x80610022U;
inline constexpr std::uint32_t decode_fatal = 0x80628002U;

struct Ringbuffer {
  std::int32_t packets{};
  std::int32_t packets_read{};
  std::int32_t packets_write_position{};
  std::int32_t packets_available{};
  std::int32_t packet_size{};
  std::uint32_t data{};
  std::uint32_t callback{};
  std::uint32_t callback_argument{};
  std::uint32_t data_upper_bound{};
  std::int32_t semaphore{};
  std::uint32_t mpeg{};
};

struct AccessUnit {
  // SceMpegAu stores each 64-bit timestamp as two words in MSB/LSB order.
  // A native int64_t has the opposite word order on little-endian hosts.
  std::uint32_t presentation_timestamp_msb{};
  std::uint32_t presentation_timestamp_lsb{};
  std::uint32_t decode_timestamp_msb{};
  std::uint32_t decode_timestamp_lsb{};
  std::uint32_t elementary_stream_buffer{};
  std::uint32_t elementary_stream_size{};
};

static_assert(sizeof(Ringbuffer) == 44U);
static_assert(sizeof(AccessUnit) == 24U);
static_assert(offsetof(AccessUnit, presentation_timestamp_msb) == 0U);
static_assert(offsetof(AccessUnit, presentation_timestamp_lsb) == 4U);
static_assert(offsetof(AccessUnit, decode_timestamp_msb) == 8U);
static_assert(offsetof(AccessUnit, decode_timestamp_lsb) == 12U);

inline void write_timestamp(std::uint32_t& msb, std::uint32_t& lsb,
                            std::int64_t value) {
  const auto bits = static_cast<std::uint64_t>(value);
  msb = static_cast<std::uint32_t>(bits >> 32U);
  lsb = static_cast<std::uint32_t>(bits);
}

inline void write_presentation_timestamp(AccessUnit& access_unit,
                                         std::int64_t value) {
  write_timestamp(access_unit.presentation_timestamp_msb,
                  access_unit.presentation_timestamp_lsb, value);
}

inline void write_decode_timestamp(AccessUnit& access_unit,
                                   std::int64_t value) {
  write_timestamp(access_unit.decode_timestamp_msb,
                  access_unit.decode_timestamp_lsb, value);
}

template <typename T>
T* guest_pointer(psprecomp::State& state, std::uint32_t address) {
  return reinterpret_cast<T*>(
      psprecomp::mapped_address(state, address, sizeof(T)));
}

inline Ringbuffer* ringbuffer_from_mpeg(psprecomp::State& state,
                                        std::uint32_t mpeg_address) {
  const auto* handle = guest_pointer<std::uint32_t>(state, mpeg_address);
  if (handle == nullptr) return nullptr;
  const auto* ringbuffer_address =
      guest_pointer<std::uint32_t>(state, *handle + 16U);
  if (ringbuffer_address == nullptr) return nullptr;
  return guest_pointer<Ringbuffer>(state, *ringbuffer_address);
}

inline std::shared_ptr<MediaEngine> engine_from_mpeg(std::uint32_t address) {
  return find_media_engine(psprecomp::canonical_address(address));
}

inline void update_ringbuffer_usage(Ringbuffer& ringbuffer,
                                    const MediaEngine& engine) {
  const auto used = std::min<std::size_t>(
      static_cast<std::size_t>(std::max(ringbuffer.packets, 0)),
      engine.packets_in_use(ringbuffer_packet_size));
  ringbuffer.packets_available = static_cast<std::int32_t>(used);
}

constexpr std::uint64_t ringbuffer_memory_size(std::uint32_t packet_count) {
  return static_cast<std::uint64_t>(packet_count) *
         (ringbuffer_packet_size + ringbuffer_packet_overhead);
}

} // namespace mpeg_state
