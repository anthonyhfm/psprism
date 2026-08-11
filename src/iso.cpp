#include "iso.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>

namespace psprecomp {
namespace {

constexpr std::uint32_t sector_size = 2048U;

std::uint16_t u16(const std::vector<std::uint8_t> &data, std::size_t offset) {
  if (offset > data.size() || data.size() - offset < 2U) {
    throw std::runtime_error("truncated PSP metadata");
  }
  return static_cast<std::uint16_t>(data[offset]) |
         static_cast<std::uint16_t>(data[offset + 1U]) << 8U;
}

std::uint32_t u32(const std::vector<std::uint8_t> &data, std::size_t offset) {
  if (offset > data.size() || data.size() - offset < 4U) {
    throw std::runtime_error("truncated ISO/PSP metadata");
  }
  return static_cast<std::uint32_t>(data[offset]) |
         static_cast<std::uint32_t>(data[offset + 1U]) << 8U |
         static_cast<std::uint32_t>(data[offset + 2U]) << 16U |
         static_cast<std::uint32_t>(data[offset + 3U]) << 24U;
}

std::string normalized(std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const auto character : value) {
    if (character == '\\') {
      result.push_back('/');
    } else if (character != '/') {
      result.push_back(static_cast<char>(
          std::toupper(static_cast<unsigned char>(character))));
    } else if (!result.empty() && result.back() != '/') {
      result.push_back('/');
    }
  }
  while (!result.empty() && result.front() == '/') {
    result.erase(result.begin());
  }
  while (!result.empty() && result.back() == '/') {
    result.pop_back();
  }
  return result;
}

std::string record_name(const std::uint8_t *record, std::size_t length) {
  if (length < 34U) {
    throw std::runtime_error("invalid ISO 9660 directory record");
  }
  const auto name_length = record[32];
  if (33U + name_length > length) {
    throw std::runtime_error("truncated ISO 9660 file name");
  }
  if (name_length == 1U && (record[33] == 0U || record[33] == 1U)) {
    return {};
  }
  std::string name(reinterpret_cast<const char *>(record + 33U), name_length);
  if (const auto version = name.find(';'); version != std::string::npos) {
    name.resize(version);
  }
  while (!name.empty() && name.back() == '.') {
    name.pop_back();
  }
  if (name.empty() || name == "." || name == ".." ||
      name.find('/') != std::string::npos ||
      name.find('\\') != std::string::npos) {
    throw std::runtime_error("unsafe ISO 9660 file name");
  }
  return name;
}

std::vector<std::uint8_t> read_range(const std::filesystem::path &path,
                                     std::uint64_t offset, std::size_t size) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream) {
    throw std::runtime_error("cannot open ISO image: " + path.string());
  }
  const auto file_size = stream.tellg();
  if (file_size < 0 || offset > static_cast<std::uint64_t>(file_size) ||
      size > static_cast<std::uint64_t>(file_size) - offset) {
    throw std::runtime_error("ISO extent is outside the image");
  }
  stream.seekg(static_cast<std::streamoff>(offset));
  std::vector<std::uint8_t> result(size);
  if (size != 0U) {
    stream.read(reinterpret_cast<char *>(result.data()),
                static_cast<std::streamsize>(size));
  }
  if (!stream) {
    throw std::runtime_error("failed while reading ISO image");
  }
  return result;
}

std::string sfo_string(const std::vector<std::uint8_t> &data,
                       std::string_view wanted_key) {
  if (data.size() < 20U || u32(data, 0) != 0x46535000U) {
    return {};
  }
  const auto key_table = u32(data, 8);
  const auto data_table = u32(data, 12);
  const auto count = u32(data, 16);
  if (count > (data.size() - 20U) / 16U) {
    return {};
  }
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto entry = 20U + static_cast<std::size_t>(i) * 16U;
    const auto key_offset =
        static_cast<std::size_t>(key_table) + u16(data, entry);
    const auto value_length = u32(data, entry + 4U);
    const auto value_offset =
        static_cast<std::size_t>(data_table) + u32(data, entry + 12U);
    if (key_offset >= data.size() || value_offset > data.size() ||
        value_length > data.size() - value_offset) {
      continue;
    }
    auto key_end = key_offset;
    while (key_end < data.size() && data[key_end] != 0U) {
      ++key_end;
    }
    const std::string_view key(
        reinterpret_cast<const char *>(data.data() + key_offset),
        key_end - key_offset);
    if (key != wanted_key) {
      continue;
    }
    auto length = static_cast<std::size_t>(value_length);
    while (length != 0U && data[value_offset + length - 1U] == 0U) {
      --length;
    }
    return std::string(
        reinterpret_cast<const char *>(data.data() + value_offset), length);
  }
  return {};
}

} // namespace

IsoImage::IsoImage(std::filesystem::path path) : path_(std::move(path)) {
  const auto descriptor = read_range(path_, 16ULL * sector_size, sector_size);
  if (descriptor[0] != 1U ||
      std::string_view(reinterpret_cast<const char *>(descriptor.data() + 1U),
                       5U) != "CD001" ||
      descriptor[6] != 1U) {
    throw std::runtime_error("input is not an ISO 9660 image");
  }

  const auto root_length = descriptor[156];
  if (root_length < 34U || 156U + root_length > descriptor.size()) {
    throw std::runtime_error("ISO image has an invalid root directory");
  }
  const auto root_extent = u32(descriptor, 158U);
  const auto root_size = u32(descriptor, 166U);

  struct PendingDirectory {
    std::filesystem::path path;
    std::uint32_t extent;
    std::uint32_t size;
  };
  std::vector<PendingDirectory> pending{{{}, root_extent, root_size}};
  std::set<std::pair<std::uint32_t, std::uint32_t>> visited;
  while (!pending.empty()) {
    const auto directory = pending.back();
    pending.pop_back();
    if (!visited.emplace(directory.extent, directory.size).second) {
      continue;
    }
    const auto bytes = read_range(
        path_, static_cast<std::uint64_t>(directory.extent) * sector_size,
        directory.size);
    std::size_t cursor = 0;
    while (cursor < bytes.size()) {
      const auto length = bytes[cursor];
      if (length == 0U) {
        cursor = ((cursor / sector_size) + 1U) * sector_size;
        continue;
      }
      if (cursor + length > bytes.size() || length < 34U) {
        throw std::runtime_error("invalid ISO directory record");
      }
      const auto *record = bytes.data() + cursor;
      const auto name = record_name(record, length);
      if (!name.empty()) {
        const auto extent = u32(bytes, cursor + 2U);
        const auto size = u32(bytes, cursor + 10U);
        const bool is_directory = (record[25] & 2U) != 0U;
        auto entry_path = directory.path / name;
        entries_.push_back({entry_path, is_directory, extent, size});
        if (is_directory) {
          pending.push_back({std::move(entry_path), extent, size});
        }
      }
      cursor += length;
    }
  }
  std::sort(entries_.begin(), entries_.end(),
            [](const IsoEntry &left, const IsoEntry &right) {
              return left.path.generic_string() < right.path.generic_string();
            });
}

const std::filesystem::path &IsoImage::path() const noexcept { return path_; }

const std::vector<IsoEntry> &IsoImage::entries() const noexcept {
  return entries_;
}

std::optional<IsoEntry> IsoImage::find(std::string_view path) const {
  const auto wanted = normalized(path);
  const auto found = std::find_if(
      entries_.begin(), entries_.end(), [&](const IsoEntry &entry) {
        return normalized(entry.path.generic_string()) == wanted;
      });
  return found == entries_.end() ? std::nullopt
                                 : std::optional<IsoEntry>(*found);
}

std::vector<std::uint8_t> IsoImage::read(const IsoEntry &entry) const {
  if (entry.directory) {
    throw std::runtime_error("cannot read an ISO directory as a file");
  }
  return read_range(path_,
                    static_cast<std::uint64_t>(entry.extent) * sector_size,
                    entry.size);
}

std::vector<std::uint8_t> IsoImage::read(std::string_view path) const {
  const auto entry = find(path);
  if (!entry) {
    throw std::runtime_error("file not found in ISO: " + std::string(path));
  }
  return read(*entry);
}

void IsoImage::extract_all(const std::filesystem::path &directory) const {
  std::ifstream source(path_, std::ios::binary | std::ios::ate);
  if (!source) {
    throw std::runtime_error("cannot open ISO image: " + path_.string());
  }
  const auto source_size = source.tellg();
  if (source_size < 0) {
    throw std::runtime_error("cannot determine ISO image size");
  }
  std::vector<char> buffer(1024U * 1024U);
  for (const auto &entry : entries_) {
    const auto destination = directory / entry.path;
    if (entry.directory) {
      std::filesystem::create_directories(destination);
      continue;
    }
    std::filesystem::create_directories(destination.parent_path());
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output) {
      throw std::runtime_error("cannot extract ISO file: " +
                               destination.string());
    }
    const auto offset = static_cast<std::uint64_t>(entry.extent) * sector_size;
    if (offset > static_cast<std::uint64_t>(source_size) ||
        entry.size > static_cast<std::uint64_t>(source_size) - offset) {
      throw std::runtime_error("ISO file extent is outside the image");
    }
    source.clear();
    source.seekg(static_cast<std::streamoff>(offset));
    if (!source) {
      throw std::runtime_error("cannot seek to ISO file: " +
                               entry.path.string());
    }
    std::uint32_t remaining = entry.size;
    while (remaining != 0U) {
      const auto amount = std::min<std::size_t>(remaining, buffer.size());
      source.read(buffer.data(), static_cast<std::streamsize>(amount));
      if (!source) {
        throw std::runtime_error("failed while reading ISO file: " +
                                 entry.path.string());
      }
      output.write(buffer.data(), static_cast<std::streamsize>(amount));
      remaining -= static_cast<std::uint32_t>(amount);
    }
    if (!output) {
      throw std::runtime_error("failed while extracting ISO file: " +
                               destination.string());
    }
  }
}

PspDiscMetadata read_psp_disc_metadata(const IsoImage &image) {
  PspDiscMetadata result;
  if (const auto sfo = image.find("PSP_GAME/PARAM.SFO")) {
    const auto data = image.read(*sfo);
    result.title = sfo_string(data, "TITLE");
    result.disc_id = sfo_string(data, "DISC_ID");
    result.sfo_path = "PSP_GAME/PARAM.SFO";
  } else if (const auto sfo_root = image.find("PARAM.SFO")) {
    const auto data = image.read(*sfo_root);
    result.title = sfo_string(data, "TITLE");
    result.disc_id = sfo_string(data, "DISC_ID");
    result.sfo_path = "PARAM.SFO";
  }
  return result;
}

std::optional<IsoEntry> find_psp_executable(const IsoImage &image) {
  constexpr std::array candidates{
      "PSP_GAME/SYSDIR/EBOOT.BIN",
      "PSP_GAME/SYSDIR/BOOT.BIN",
  };
  std::optional<IsoEntry> fallback;
  for (const auto *path : candidates) {
    const auto entry = image.find(path);
    if (!entry || entry->directory) {
      continue;
    }
    if (!fallback) {
      fallback = entry;
    }
    const auto prefix = image.read(*entry);
    if (prefix.size() >= 4U && ((prefix[0] == 0x7fU && prefix[1] == 'E' &&
                                 prefix[2] == 'L' && prefix[3] == 'F') ||
                                (prefix[0] == '~' && prefix[1] == 'P' &&
                                 prefix[2] == 'S' && prefix[3] == 'P'))) {
      return entry;
    }
  }
  if (fallback) {
    return fallback;
  }

  const auto cnf_entry = image.find("SYSTEM.CNF");
  if (cnf_entry && !cnf_entry->directory) {
    const auto cnf_bytes = image.read(*cnf_entry);
    const std::string_view cnf_text(
        reinterpret_cast<const char *>(cnf_bytes.data()), cnf_bytes.size());
    std::size_t line_start = 0;
    while (line_start < cnf_text.size()) {
      auto line_end = cnf_text.find('\n', line_start);
      if (line_end == std::string_view::npos) {
        line_end = cnf_text.size();
      }
      auto line = cnf_text.substr(line_start, line_end - line_start);
      line_start = line_end + 1;
      if (const auto cr = line.find('\r'); cr != std::string_view::npos) {
        line = line.substr(0, cr);
      }
      if (const auto eq = line.find('='); eq != std::string_view::npos) {
        auto key = line.substr(0, eq);
        while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) {
          key.remove_suffix(1);
        }
        while (!key.empty() && std::isspace(static_cast<unsigned char>(key.front()))) {
          key.remove_prefix(1);
        }
        if (key == "BOOT2" || key == "BOOT" || key == "boot2" || key == "boot") {
          auto val = line.substr(eq + 1);
          while (!val.empty() && std::isspace(static_cast<unsigned char>(val.front()))) {
            val.remove_prefix(1);
          }
          while (!val.empty() && std::isspace(static_cast<unsigned char>(val.back()))) {
            val.remove_suffix(1);
          }
          if (val.rfind("cdrom0:", 0) == 0 || val.rfind("CDROM0:", 0) == 0) {
            val.remove_prefix(7);
          }
          while (!val.empty() && (val.front() == '\\' || val.front() == '/')) {
            val.remove_prefix(1);
          }
          if (const auto semi = val.find(';'); semi != std::string_view::npos) {
            val = val.substr(0, semi);
          }
          const auto target_entry = image.find(val);
          if (target_entry && !target_entry->directory) {
            return target_entry;
          }
        }
      }
    }
  }

  for (const auto &entry : image.entries()) {
    if (entry.directory || entry.size < 4U) {
      continue;
    }
    const auto prefix = image.read(entry);
    if (prefix.size() >= 4U && ((prefix[0] == 0x7fU && prefix[1] == 'E' &&
                                 prefix[2] == 'L' && prefix[3] == 'F') ||
                                (prefix[0] == '~' && prefix[1] == 'P' &&
                                 prefix[2] == 'S' && prefix[3] == 'P'))) {
      return entry;
    }
  }

  return std::nullopt;
}

} // namespace psprecomp
