#pragma once

#include "ge_command.hpp"

#include <cstddef>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace refract::ge {

inline constexpr std::uint32_t trace_magic = 0x52544750U; // PGTR
inline constexpr std::uint16_t trace_version = 1U;

struct TraceRecord {
  std::uint32_t list_id{};
  std::uint32_t program_counter{};
  std::uint32_t instruction{};

  friend bool operator==(const TraceRecord&, const TraceRecord&) = default;
};

class Trace {
public:
  void set_enabled(bool enabled) { enabled_ = enabled; }
  bool enabled() const { return enabled_; }
  void clear() { records_.clear(); }

  void record(std::uint32_t list_id, std::uint32_t program_counter,
              std::uint32_t instruction) {
    if (enabled_) records_.push_back({list_id, program_counter, instruction});
  }

  std::span<const TraceRecord> records() const { return records_; }

  std::array<std::uint32_t, 256> command_coverage() const {
    std::array<std::uint32_t, 256> result{};
    for (const auto& record : records_) ++result[opcode(record.instruction)];
    return result;
  }

  std::array<std::uint32_t, 256> replayed_command_state() const {
    std::array<std::uint32_t, 256> result{};
    replay([&](const TraceRecord& record) {
      result[opcode(record.instruction)] = record.instruction;
    });
    return result;
  }

  std::vector<std::uint8_t> serialize() const {
    constexpr std::size_t header_size = 12U;
    constexpr std::size_t record_size = 12U;
    std::vector<std::uint8_t> result(
        header_size + records_.size() * record_size);
    const auto count = static_cast<std::uint32_t>(records_.size());
    const auto write16 = [&](std::size_t offset, std::uint16_t value) {
      result[offset] = static_cast<std::uint8_t>(value);
      result[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    };
    const auto write32 = [&](std::size_t offset, std::uint32_t value) {
      for (std::size_t byte = 0; byte < 4U; ++byte)
        result[offset + byte] =
            static_cast<std::uint8_t>(value >> (byte * 8U));
    };
    write32(0U, trace_magic);
    write16(4U, trace_version);
    write16(6U, record_size);
    write32(8U, count);
    for (std::size_t index = 0; index < records_.size(); ++index) {
      const auto offset = header_size + index * record_size;
      write32(offset, records_[index].list_id);
      write32(offset + 4U, records_[index].program_counter);
      write32(offset + 8U, records_[index].instruction);
    }
    return result;
  }

  static bool deserialize(std::span<const std::uint8_t> input, Trace& output) {
    constexpr std::size_t header_size = 12U;
    constexpr std::size_t expected_record_size = 12U;
    if (input.size() < header_size) return false;
    const auto read16 = [&](std::size_t offset) {
      return static_cast<std::uint16_t>(input[offset]) |
             static_cast<std::uint16_t>(input[offset + 1U]) << 8U;
    };
    const auto read32 = [&](std::size_t offset) {
      std::uint32_t value{};
      for (std::size_t byte = 0; byte < 4U; ++byte)
        value |= static_cast<std::uint32_t>(input[offset + byte])
                 << (byte * 8U);
      return value;
    };
    const auto magic = read32(0U);
    const auto version = read16(4U);
    const auto record_size = read16(6U);
    const auto count = read32(8U);
    if (magic != trace_magic || version != trace_version ||
        record_size != expected_record_size ||
        count > (input.size() - header_size) / expected_record_size ||
        header_size + static_cast<std::size_t>(count) * expected_record_size !=
            input.size())
      return false;
    output.records_.resize(count);
    for (std::size_t index = 0; index < count; ++index) {
      const auto offset = header_size + index * expected_record_size;
      output.records_[index] =
          {read32(offset), read32(offset + 4U), read32(offset + 8U)};
    }
    return true;
  }

  template <typename Consumer> void replay(Consumer&& consumer) const {
    for (const auto& record : records_) consumer(record);
  }

private:
  bool enabled_{};
  std::vector<TraceRecord> records_;
};

} // namespace refract::ge
