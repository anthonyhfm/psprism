#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace psprecomp {

enum class InputKind { executable, iso };

struct SourceInfo {
  InputKind kind{InputKind::executable};
  std::string suggested_display_name;
  std::string disc_id;
  std::string executable_path;
  std::string sfo_path;
  bool executable_encrypted{};
  std::size_t disc_entries{};
};

struct ExportConfig {
  std::filesystem::path input;
  std::filesystem::path output_directory;
  std::filesystem::path runtime_include_directory;
  std::filesystem::path refract_directory;
  std::optional<std::filesystem::path> code_map;
  std::string display_name;
  std::string project_name;
  std::string disc_id;
  bool extract_disc{true};
  std::function<void(std::string_view)> progress;
};

struct ExportSummary {
  InputKind input_kind{InputKind::executable};
  std::filesystem::path output_directory;
  std::filesystem::path executable_source;
  std::string decryption_backend;
  std::size_t disc_entries{};
  std::size_t generated_translation_units{};
};

[[nodiscard]] SourceInfo inspect_source(const std::filesystem::path &input);
[[nodiscard]] std::string project_slug(std::string_view value);
[[nodiscard]] ExportSummary export_codebase(const ExportConfig &config);

} // namespace psprecomp
