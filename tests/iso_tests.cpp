#include "decrypt.hpp"
#include "iso.hpp"

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

constexpr std::size_t sector_size = 2048U;

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

std::size_t directory_record(std::vector<std::uint8_t> &image,
                             std::size_t offset, std::uint32_t extent,
                             std::uint32_t size, bool directory,
                             const std::vector<std::uint8_t> &name) {
  const auto length = 33U + name.size() + ((33U + name.size()) & 1U);
  image[offset] = static_cast<std::uint8_t>(length);
  put32(image, offset + 2U, extent);
  put32(image, offset + 10U, size);
  image[offset + 25U] = directory ? 2U : 0U;
  put16(image, offset + 28U, 1U);
  image[offset + 32U] = static_cast<std::uint8_t>(name.size());
  std::copy(name.begin(), name.end(), image.begin() + offset + 33U);
  return length;
}

std::vector<std::uint8_t> make_sfo() {
  const std::string keys = std::string("TITLE\0DISC_ID\0", 14U);
  const std::string title = std::string("Wizard Fixture\0", 15U);
  const std::string disc = std::string("TEST00001\0", 10U);
  constexpr std::uint32_t key_offset = 52U;
  const auto data_offset = key_offset + static_cast<std::uint32_t>(keys.size());
  std::vector<std::uint8_t> result(data_offset + title.size() + disc.size());
  put32(result, 0U, 0x46535000U);
  put32(result, 4U, 0x00000101U);
  put32(result, 8U, key_offset);
  put32(result, 12U, data_offset);
  put32(result, 16U, 2U);
  put16(result, 20U, 0U);
  put16(result, 22U, 0x0204U);
  put32(result, 24U, static_cast<std::uint32_t>(title.size()));
  put32(result, 28U, static_cast<std::uint32_t>(title.size()));
  put32(result, 32U, 0U);
  put16(result, 36U, 6U);
  put16(result, 38U, 0x0204U);
  put32(result, 40U, static_cast<std::uint32_t>(disc.size()));
  put32(result, 44U, static_cast<std::uint32_t>(disc.size()));
  put32(result, 48U, static_cast<std::uint32_t>(title.size()));
  std::copy(keys.begin(), keys.end(), result.begin() + key_offset);
  std::copy(title.begin(), title.end(), result.begin() + data_offset);
  std::copy(disc.begin(), disc.end(),
            result.begin() + data_offset + title.size());
  return result;
}

std::vector<std::uint8_t> make_iso() {
  std::vector<std::uint8_t> image(28U * sector_size);
  const auto pvd = 16U * sector_size;
  image[pvd] = 1U;
  std::copy_n("CD001", 5U, image.begin() + pvd + 1U);
  image[pvd + 6U] = 1U;
  directory_record(image, pvd + 156U, 20U, sector_size, true, {0U});
  const auto terminator = 17U * sector_size;
  image[terminator] = 255U;
  std::copy_n("CD001", 5U, image.begin() + terminator + 1U);
  image[terminator + 6U] = 1U;

  auto cursor = 20U * sector_size;
  cursor += directory_record(image, cursor, 20U, sector_size, true, {0U});
  cursor += directory_record(image, cursor, 20U, sector_size, true, {1U});
  directory_record(image, cursor, 21U, sector_size, true,
                   {'P', 'S', 'P', '_', 'G', 'A', 'M', 'E'});

  const auto sfo = make_sfo();
  cursor = 21U * sector_size;
  cursor += directory_record(image, cursor, 21U, sector_size, true, {0U});
  cursor += directory_record(image, cursor, 20U, sector_size, true, {1U});
  cursor += directory_record(image, cursor, 22U, sector_size, true,
                             {'S', 'Y', 'S', 'D', 'I', 'R'});
  directory_record(image, cursor, 24U, static_cast<std::uint32_t>(sfo.size()),
                   false,
                   {'P', 'A', 'R', 'A', 'M', '.', 'S', 'F', 'O', ';', '1'});

  cursor = 22U * sector_size;
  cursor += directory_record(image, cursor, 22U, sector_size, true, {0U});
  cursor += directory_record(image, cursor, 21U, sector_size, true, {1U});
  directory_record(image, cursor, 23U, 8U, false,
                   {'E', 'B', 'O', 'O', 'T', '.', 'B', 'I', 'N', ';', '1'});
  const std::uint8_t executable[]{0x7fU, 'E', 'L', 'F', 1U, 2U, 3U, 4U};
  std::copy(std::begin(executable), std::end(executable),
            image.begin() + 23U * sector_size);
  std::copy(sfo.begin(), sfo.end(), image.begin() + 24U * sector_size);
  return image;
}

} // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto temporary = std::filesystem::temp_directory_path() /
                         ("psprecomp-iso-test-" + std::to_string(nonce));
  std::filesystem::create_directories(temporary);
  const auto iso_path = temporary / "fixture.iso";
  const auto image = make_iso();
  {
    std::ofstream stream(iso_path, std::ios::binary);
    stream.write(reinterpret_cast<const char *>(image.data()),
                 static_cast<std::streamsize>(image.size()));
  }

  const psprecomp::IsoImage iso(iso_path);
  CHECK(iso.entries().size() == 4U);
  const auto executable = psprecomp::find_psp_executable(iso);
  CHECK(executable.has_value());
  CHECK(executable->path.generic_string() == "PSP_GAME/SYSDIR/EBOOT.BIN");
  const auto executable_data = iso.read(*executable);
  CHECK(executable_data.size() == 8U);
  CHECK(executable_data[0] == 0x7fU);
  CHECK(psprecomp::is_elf_data(executable_data));
  CHECK(!psprecomp::is_encrypted_psp_data(executable_data));
  CHECK(psprecomp::is_encrypted_psp_data({'~', 'P', 'S', 'P'}));
  const auto metadata = psprecomp::read_psp_disc_metadata(iso);
  CHECK(metadata.title == "Wizard Fixture");
  CHECK(metadata.disc_id == "TEST00001");

  iso.extract_all(temporary / "disc");
  CHECK(std::filesystem::file_size(temporary /
                                   "disc/PSP_GAME/SYSDIR/EBOOT.BIN") == 8U);
  std::filesystem::remove_all(temporary);
  return 0;
}
