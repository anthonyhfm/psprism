#pragma once

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace io_state {

constexpr std::uint64_t sector_size = 2048U;

struct FileView {
  std::uint64_t base{};
  std::uint64_t size{};

  bool operator==(const FileView&) const = default;
};

inline bool is_raw_disc_path(std::string_view path) {
  return (path.starts_with("disc0:") || path.starts_with("umd0:")) &&
         path.find("/sce_lbn0x") != std::string_view::npos;
}

inline bool is_whole_disc_path(std::string_view path) {
  return path == "umd0:" || path == "umd0:/";
}

inline std::optional<std::uint64_t> sector_byte_offset(std::uint64_t sector) {
  if (sector > std::numeric_limits<std::uint64_t>::max() / sector_size)
    return std::nullopt;
  return sector * sector_size;
}

inline std::optional<std::size_t> sector_byte_count(std::uint64_t count) {
  const auto bytes = sector_byte_offset(count);
  if (!bytes || *bytes > std::numeric_limits<std::size_t>::max())
    return std::nullopt;
  return static_cast<std::size_t>(*bytes);
}

inline std::uint64_t complete_sector_count(std::uint64_t bytes) {
  return bytes / sector_size;
}

inline std::int64_t signed_from_words(std::uint32_t low, std::uint32_t high) {
  const auto bits = static_cast<std::uint64_t>(low) |
                    static_cast<std::uint64_t>(high) << 32U;
  return std::bit_cast<std::int64_t>(bits);
}

inline std::optional<FileView> parse_raw_disc_view(std::string_view path) {
  if (!is_raw_disc_path(path))
    return std::nullopt;
  constexpr std::string_view lbn_marker = "/sce_lbn0x";
  constexpr std::string_view size_marker = "_size0x";
  const auto lbn_begin = path.find(lbn_marker) + lbn_marker.size();
  const auto lbn_end = path.find(size_marker, lbn_begin);
  if (lbn_end == std::string_view::npos || lbn_end == lbn_begin)
    return std::nullopt;
  const auto size_begin = lbn_end + size_marker.size();
  if (size_begin == path.size())
    return std::nullopt;

  std::uint64_t lbn{};
  const auto lbn_text = path.substr(lbn_begin, lbn_end - lbn_begin);
  const auto lbn_result =
      std::from_chars(lbn_text.data(), lbn_text.data() + lbn_text.size(), lbn,
                      16);
  std::uint64_t size{};
  const auto size_text = path.substr(size_begin);
  const auto size_result = std::from_chars(
      size_text.data(), size_text.data() + size_text.size(), size, 16);
  if (lbn_result.ec != std::errc{} ||
      lbn_result.ptr != lbn_text.data() + lbn_text.size() ||
      size_result.ec != std::errc{} ||
      size_result.ptr != size_text.data() + size_text.size() ||
      lbn > std::numeric_limits<std::uint64_t>::max() / sector_size)
    return std::nullopt;
  const auto base = lbn * sector_size;
  if (size > std::numeric_limits<std::uint64_t>::max() - base)
    return std::nullopt;
  return FileView{base, size};
}

inline std::size_t readable_size(const FileView& view,
                                 std::uint64_t absolute_position,
                                 std::size_t requested) {
  if (absolute_position < view.base ||
      absolute_position >= view.base + view.size)
    return 0U;
  const auto remaining = view.base + view.size - absolute_position;
  return static_cast<std::size_t>(std::min<std::uint64_t>(requested, remaining));
}

inline std::optional<std::uint64_t> add_signed(std::uint64_t base,
                                               std::int64_t offset) {
  if (offset < 0) {
    const auto magnitude = static_cast<std::uint64_t>(-(offset + 1)) + 1U;
    if (magnitude > base)
      return std::nullopt;
    return base - magnitude;
  }
  const auto magnitude = static_cast<std::uint64_t>(offset);
  if (magnitude > std::numeric_limits<std::uint64_t>::max() - base)
    return std::nullopt;
  return base + magnitude;
}

} // namespace io_state
