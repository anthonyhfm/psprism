#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace psprecomp {

struct IsoEntry {
  std::filesystem::path path;
  bool directory{};
  std::uint32_t extent{};
  std::uint32_t size{};
};

struct PspDiscMetadata {
  std::string title;
  std::string disc_id;
};

class IsoImage {
public:
  explicit IsoImage(std::filesystem::path path);

  [[nodiscard]] const std::filesystem::path &path() const noexcept;
  [[nodiscard]] const std::vector<IsoEntry> &entries() const noexcept;
  [[nodiscard]] std::optional<IsoEntry> find(std::string_view path) const;
  [[nodiscard]] std::vector<std::uint8_t> read(const IsoEntry &entry) const;
  [[nodiscard]] std::vector<std::uint8_t> read(std::string_view path) const;
  void extract_all(const std::filesystem::path &directory,
                   const std::function<bool()> &is_cancel_requested = {}) const;

private:
  std::filesystem::path path_;
  std::vector<IsoEntry> entries_;
};

[[nodiscard]] PspDiscMetadata read_psp_disc_metadata(const IsoImage &image);
[[nodiscard]] std::optional<IsoEntry>
find_psp_executable(const IsoImage &image);

} // namespace psprecomp
