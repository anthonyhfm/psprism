#pragma once

#include "../project.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace psprecomp::project_detail {

std::vector<std::uint8_t> read_binary(const std::filesystem::path& path);
bool has_psp_executable_magic(const std::filesystem::path& path);
InputKind detect_kind(const std::filesystem::path& input);
void write_bytes(const std::filesystem::path& path,
                 const std::vector<std::uint8_t>& data);
void write_text(const std::filesystem::path& path, std::string_view value);
std::string toml_string(std::string_view value);
std::string generated_readme(const ExportConfig& config, InputKind kind,
                             std::string_view executable_source);
std::string root_makefile(const ExportConfig& config, bool has_disc,
                          std::string_view disc_executable,
                          std::string_view sfo_path,
                          std::string_view psp_recompile_mode,
                          std::string_view hydration_input);
std::string iso_patch_tool_source();
std::string patch_template_source();
std::string patch_tutorial_readme();

} // namespace psprecomp::project_detail
