#pragma once

#include <psprecomp/runtime.hpp>

#include <cstdint>
#include <cstring>

namespace mpeg_state {

inline constexpr std::uint32_t avc_es_size = 2048U;
inline constexpr std::uint32_t atrac_es_size = 2112U;
inline constexpr std::uint32_t atrac_es_output_size = 8192U;
inline constexpr std::uint32_t required_memory_size = 0x10000U;
inline constexpr std::uint32_t ringbuffer_packet_size = 2048U;
inline constexpr std::uint32_t ringbuffer_packet_overhead = 104U;
inline constexpr std::uint32_t no_data = 0x80618001U;

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
  std::uint32_t gp{};
};

struct AccessUnit {
  std::int64_t presentation_timestamp{};
  std::int64_t decode_timestamp{};
  std::uint32_t elementary_stream_buffer{};
  std::uint32_t elementary_stream_size{};
};

static_assert(sizeof(Ringbuffer) == 48U);
static_assert(sizeof(AccessUnit) == 24U);

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

constexpr std::uint32_t ringbuffer_memory_size(std::uint32_t packet_count) {
  return packet_count *
         (ringbuffer_packet_size + ringbuffer_packet_overhead);
}

} // namespace mpeg_state
