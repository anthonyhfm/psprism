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

struct ProjectManifest {
  std::uint32_t format_version{1U};
  std::string display_name;
  std::string project_name;
  std::string disc_id;
  InputKind input_kind{InputKind::executable};
  std::string executable_path;
  std::string sfo_path;
  std::filesystem::path code_map;
  std::string psp_recompile_mode{"full"};
  bool disc_extracted{};
  std::string input_sha256;
  std::string executable_sha256;
};

struct HydrateConfig {
  std::filesystem::path input;
  std::filesystem::path project_directory;
  std::filesystem::path runtime_include_directory;
  std::filesystem::path refract_directory;
  std::filesystem::path toolchain_executable;
  std::string toolchain_revision;
  bool force{};
  std::function<void(std::string_view)> progress;
};

struct HydrateSummary {
  bool cached{};
  std::string input_sha256;
  std::string executable_sha256;
  std::size_t generated_translation_units{};
};

[[nodiscard]] SourceInfo inspect_source(const std::filesystem::path &input);
[[nodiscard]] std::string project_slug(std::string_view value);
[[nodiscard]] ExportSummary export_codebase(const ExportConfig &config);
[[nodiscard]] ProjectManifest
load_project_manifest(const std::filesystem::path &path);
[[nodiscard]] HydrateSummary hydrate_codebase(const HydrateConfig &config);

} // namespace psprecomp
