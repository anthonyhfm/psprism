#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace psprecomp::tui {

struct InitOptions {
  std::filesystem::path input;
  std::filesystem::path output;
  std::optional<std::filesystem::path> code_map;
  std::string display_name;
  std::string project_name;
  bool extract_disc{true};
  bool extract_disc_set{};
};

struct Context {
  std::filesystem::path runtime_include_directory;
  std::filesystem::path psprism_directory;
};

[[nodiscard]] bool supported_terminal();
int run_wizard(InitOptions options, const Context& context);

}  // namespace psprecomp::tui
