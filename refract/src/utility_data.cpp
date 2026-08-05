#include "utility_data.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace refract::utility {
namespace {

std::string fixed_string(const std::uint8_t* value, std::size_t size) {
  const auto* end = static_cast<const std::uint8_t*>(
      std::memchr(value, 0, size));
  return std::string(reinterpret_cast<const char*>(value),
                     end == nullptr ? size
                                    : static_cast<std::size_t>(end - value));
}

std::size_t align_up(std::size_t value, std::size_t alignment) {
  return (value + alignment - 1U) & ~(alignment - 1U);
}

} // namespace

std::string sfo_string(const std::vector<std::uint8_t>& data,
                       std::string_view wanted_key) {
  const auto read16 = [&](std::size_t offset) {
    std::uint16_t value{};
    if (offset + sizeof(value) <= data.size())
      std::memcpy(&value, data.data() + offset, sizeof(value));
    return value;
  };
  const auto read32 = [&](std::size_t offset) {
    std::uint32_t value{};
    if (offset + sizeof(value) <= data.size())
      std::memcpy(&value, data.data() + offset, sizeof(value));
    return value;
  };
  if (data.size() < 20U || read32(0) != 0x46535000U) return {};
  const auto keys = read32(8);
  const auto values = read32(12);
  const auto count = read32(16);
  if (count > (data.size() - 20U) / 16U) return {};
  for (std::uint32_t index = 0; index < count; ++index) {
    const auto entry = 20U + index * 16U;
    const auto key_offset = static_cast<std::size_t>(keys) + read16(entry);
    const auto length = read32(entry + 4U);
    const auto value_offset = static_cast<std::size_t>(values) +
                              read32(entry + 12U);
    if (key_offset >= data.size() || value_offset > data.size() ||
        length > data.size() - value_offset)
      continue;
    const auto key = fixed_string(data.data() + key_offset,
                                  data.size() - key_offset);
    if (key == wanted_key)
      return fixed_string(data.data() + value_offset, length);
  }
  return {};
}

std::vector<std::uint8_t> make_savedata_sfo(std::string_view title,
                                             std::string_view savedata_title,
                                             std::string_view detail) {
  constexpr std::array<std::string_view, 3> keys{
      "TITLE", "SAVEDATA_TITLE", "SAVEDATA_DETAIL"};
  constexpr std::array<std::uint32_t, 3> capacities{128U, 128U, 1024U};
  const std::array<std::string_view, 3> values{title, savedata_title, detail};
  constexpr std::size_t entry_table = 20U;
  constexpr std::size_t key_table = entry_table + keys.size() * 16U;
  std::size_t key_bytes{};
  for (const auto key : keys) key_bytes += key.size() + 1U;
  const auto data_table = align_up(key_table + key_bytes, 4U);
  std::size_t value_bytes{};
  for (const auto capacity : capacities) value_bytes += capacity;
  std::vector<std::uint8_t> result(data_table + value_bytes, 0U);
  const auto write16 = [&](std::size_t offset, std::uint16_t value) {
    std::memcpy(result.data() + offset, &value, sizeof(value));
  };
  const auto write32 = [&](std::size_t offset, std::uint32_t value) {
    std::memcpy(result.data() + offset, &value, sizeof(value));
  };
  write32(0U, 0x46535000U);
  write32(4U, 0x00000101U);
  write32(8U, static_cast<std::uint32_t>(key_table));
  write32(12U, static_cast<std::uint32_t>(data_table));
  write32(16U, static_cast<std::uint32_t>(keys.size()));
  std::size_t key_offset{};
  std::size_t value_offset{};
  for (std::size_t index = 0; index < keys.size(); ++index) {
    const auto entry = entry_table + index * 16U;
    write16(entry, static_cast<std::uint16_t>(key_offset));
    write16(entry + 2U, 0x0204U);
    const auto length = std::min<std::size_t>(values[index].size(),
                                              capacities[index] - 1U);
    write32(entry + 4U, static_cast<std::uint32_t>(length + 1U));
    write32(entry + 8U, capacities[index]);
    write32(entry + 12U, static_cast<std::uint32_t>(value_offset));
    std::memcpy(result.data() + key_table + key_offset, keys[index].data(),
                keys[index].size());
    std::memcpy(result.data() + data_table + value_offset,
                values[index].data(), length);
    key_offset += keys[index].size() + 1U;
    value_offset += capacities[index];
  }
  return result;
}

} // namespace refract::utility
