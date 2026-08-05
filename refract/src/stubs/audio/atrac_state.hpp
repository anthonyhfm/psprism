#pragma once

#if !defined(__PSP__)

#include <mutex>
#include <unordered_map>

namespace atrac_state {

constexpr std::uint32_t invalid_id = 0x80630005U;
constexpr std::uint32_t second_buffer_not_needed = 0x80630022U;
constexpr std::uint32_t samples_per_frame = 1024U;

struct Decoder {
  std::uint32_t buffer{};
  std::uint32_t buffer_size{};
  bool decoded{};
};

inline std::mutex& mutex() {
  static std::mutex value;
  return value;
}

inline std::unordered_map<int, Decoder>& decoders() {
  static std::unordered_map<int, Decoder> value;
  return value;
}

inline int create(std::uint32_t buffer, std::uint32_t buffer_size) {
  static int next_id = 1;
  std::lock_guard lock(mutex());
  const auto id = next_id++;
  decoders()[id] = Decoder{buffer, buffer_size, false};
  return id;
}

inline bool get(int id, Decoder& output) {
  std::lock_guard lock(mutex());
  const auto found = decoders().find(id);
  if (found == decoders().end()) return false;
  output = found->second;
  return true;
}

inline bool mark_decoded(int id, bool& was_decoded) {
  std::lock_guard lock(mutex());
  const auto found = decoders().find(id);
  if (found == decoders().end()) return false;
  was_decoded = found->second.decoded;
  found->second.decoded = true;
  return true;
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

inline std::uint32_t read_u32(psprecomp::State& state,
                              std::uint32_t address) {
  std::uint32_t value{};
  if (const auto* input =
          psprecomp::mapped_address(state, address, sizeof(value)))
    std::memcpy(&value, input, sizeof(value));
  return value;
}

} // namespace atrac_state

#endif
