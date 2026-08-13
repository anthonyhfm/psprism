#include "elf.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      std::cerr << "check failed: " #expression << " at " << __FILE__ << ':'   \
                << __LINE__ << '\n';                                           \
      return 1;                                                                \
    }                                                                          \
  } while (false)

void put16(std::vector<std::uint8_t> &data, std::size_t offset,
           std::uint16_t value) {
  data[offset] = static_cast<std::uint8_t>(value);
  data[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

void put32(std::vector<std::uint8_t> &data, std::size_t offset,
           std::uint32_t value) {
  data[offset] = static_cast<std::uint8_t>(value);
  data[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
  data[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
  data[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

void section(std::vector<std::uint8_t> &data, std::size_t offset,
             std::uint32_t type, std::uint32_t flags, std::uint32_t address,
             std::uint32_t file_offset, std::uint32_t size) {
  put32(data, offset + 4U, type);
  put32(data, offset + 8U, flags);
  put32(data, offset + 12U, address);
  put32(data, offset + 16U, file_offset);
  put32(data, offset + 20U, size);
  put32(data, offset + 32U, 4U);
}

std::vector<std::uint8_t> make_stripped_prx() {
  constexpr std::uint32_t segment_offset = 0x100U;
  constexpr std::uint32_t segment_size = 0x200U;
  constexpr std::uint32_t section_offset = 0x300U;
  std::vector<std::uint8_t> data(0x400U);

  data[0] = 0x7fU;
  data[1] = 'E';
  data[2] = 'L';
  data[3] = 'F';
  data[4] = 1U;
  data[5] = 1U;
  data[6] = 1U;
  put16(data, 16U, 0xffa0U);
  put16(data, 18U, 8U);
  put32(data, 20U, 1U);
  put32(data, 24U, 0x80U);
  put32(data, 28U, 52U);
  put32(data, 32U, section_offset);
  put16(data, 40U, 52U);
  put16(data, 42U, 32U);
  put16(data, 44U, 1U);
  put16(data, 46U, 40U);
  put16(data, 48U, 3U);
  put16(data, 50U, 2U);

  put32(data, 52U, 1U);
  put32(data, 56U, segment_offset);
  put32(data, 60U, 0U);
  put32(data, 64U, segment_offset + 0x20U);
  put32(data, 68U, segment_size);
  put32(data, 72U, segment_size);
  put32(data, 76U, 5U);
  put32(data, 80U, 4U);

  const auto at = [](std::uint32_t address) {
    return static_cast<std::size_t>(segment_offset + address);
  };
  std::copy_n("fixture", 8U, data.begin() + at(0x24U));
  put32(data, at(0x40U), 0x1234U);
  put32(data, at(0x4cU), 0x60U);
  put32(data, at(0x50U), 0x74U);

  put32(data, at(0x60U), 0xa0U);
  data[at(0x68U)] = 5U;
  put16(data, at(0x6aU), 1U);
  put32(data, at(0x6cU), 0xb0U);
  put32(data, at(0x70U), 0xc0U);
  std::copy_n("TestLibrary", 12U, data.begin() + at(0xa0U));
  put32(data, at(0xb0U), 0x89abcdefU);
  put32(data, at(0xc0U), 0x03e00008U);

  section(data, section_offset + 40U, 1U, 6U, 0x80U,
          segment_offset + 0x80U, 8U);
  section(data, section_offset + 80U, 3U, 0U, 0U, 0x3f0U, 1U);
  return data;
}

} // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto path = std::filesystem::temp_directory_path() /
                    ("psprecomp-elf-test-" + std::to_string(nonce) + ".elf");
  const auto data = make_stripped_prx();
  {
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char *>(data.data()),
                 static_cast<std::streamsize>(data.size()));
  }

  const auto image = psprecomp::load_elf(path);
  CHECK(image.gp_pointer_offset == 0x40U);
  CHECK(image.gp_value_offset == 0x1234U);
  CHECK(image.imports.size() == 1U);
  CHECK(image.imports.front().library == "TestLibrary");
  CHECK(image.imports.front().nid == 0x89abcdefU);
  CHECK(image.imports.front().stub_address == 0xc0U);
  CHECK(image.imports.front().library_flags == 0U);
  std::filesystem::remove(path);

  const auto map_path = path.string() + ".map";
  {
    std::ofstream stream(map_path);
    stream << "version 2\n"
              "entry 0x20\n"
              "function_range 0x20 0x30 selected_function\n"
              "function 0x40 unchanged_function\n"
              "block 0x24\n"
              "block 0x24\n"
              "gp 0x20 0x1234\n"
              "t9 0x20 0x20\n"
              "exclude 0x80 0x90\n"
              "exclude 0x88 0xa0\n"
              "overlay 0x20\n"
              "overlay 0x20\n";
  }
  const auto map = psprecomp::load_code_map(map_path);
  CHECK(map.entry == 0x20U);
  CHECK(map.version == 2U);
  CHECK(map.function_starts.size() == 2U);
  CHECK(map.function_ranges.size() == 1U);
  CHECK(map.function_containing(0x2cU) == &map.function_ranges.front());
  CHECK(map.function_containing(0x30U) == nullptr);
  CHECK(map.block_entries.size() == 1U && map.block_entries.front() == 0x24U);
  CHECK(map.gp_values.size() == 1U && map.gp_values.front().value == 0x1234U);
  CHECK(map.t9_values.size() == 1U && map.t9_values.front().value == 0x20U);
  CHECK(map.excluded_ranges.size() == 1U);
  CHECK(!map.contains(0x98U));
  CHECK(map.contains(0xa0U));
  CHECK(map.overlay_starts.size() == 1U);
  CHECK(map.overlay_starts.front() == 0x20U);
  std::filesystem::remove(map_path);
  return 0;
}
