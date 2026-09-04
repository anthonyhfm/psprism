#include "../decrypt.hpp"
#include "../elf.hpp"
#include "../emitter.hpp"
#include "../hash.hpp"
#include "../iso.hpp"
#include "internal.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace psprecomp {
namespace {

std::string trim(std::string_view value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front()))) {
    value.remove_prefix(1U);
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back()))) {
    value.remove_suffix(1U);
  }
  return std::string(value);
}

std::string parse_string(std::string_view value, std::size_t line_number) {
  if (value.size() < 2U || value.front() != '"' || value.back() != '"') {
    throw std::runtime_error("expected quoted string in project.toml line " +
                             std::to_string(line_number));
  }
  std::string result;
  for (std::size_t index = 1U; index + 1U < value.size(); ++index) {
    auto character = value[index];
    if (character == '\\') {
      if (++index + 1U >= value.size()) {
        throw std::runtime_error("invalid escape in project.toml line " +
                                 std::to_string(line_number));
      }
      character = value[index];
      if (character == 'n') character = '\n';
      else if (character != '\\' && character != '"') {
        throw std::runtime_error("unsupported escape in project.toml line " +
                                 std::to_string(line_number));
      }
    }
    result.push_back(character);
  }
  return result;
}

bool parse_bool(std::string_view value, std::size_t line_number) {
  if (value == "true") return true;
  if (value == "false") return false;
  throw std::runtime_error("expected boolean in project.toml line " +
                           std::to_string(line_number));
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open text file: " + path.string());
  }
  return {(std::istreambuf_iterator<char>(input)),
          std::istreambuf_iterator<char>()};
}

std::filesystem::path unique_hydration_path(
    const std::filesystem::path& project) {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  return project.parent_path() /
         ("." + project.filename().string() + ".hydrate-" +
          std::to_string(nonce));
}

bool hydration_outputs_exist(const std::filesystem::path& project,
                             bool needs_disc) {
  return std::filesystem::is_regular_file(project / "src/generated/dispatch.cpp") &&
         std::filesystem::is_regular_file(project /
                                          "src/generated/guest_image.bin") &&
         std::filesystem::is_regular_file(project /
                                          "platform/macos/platform.cpp") &&
         std::filesystem::is_directory(project / "include/psprecomp") &&
         std::filesystem::is_directory(project / "refract") &&
         (!needs_disc || std::filesystem::is_directory(project / "disc"));
}

struct Replacement {
  std::filesystem::path staged;
  std::filesystem::path destination;
  std::filesystem::path backup;
  bool had_previous{};
  bool installed{};
};

void publish_replacements(std::vector<Replacement>& replacements) {
  try {
    for (auto& replacement : replacements) {
      std::filesystem::create_directories(replacement.destination.parent_path());
      if (std::filesystem::exists(replacement.destination)) {
        std::filesystem::create_directories(replacement.backup.parent_path());
        std::filesystem::rename(replacement.destination, replacement.backup);
        replacement.had_previous = true;
      }
      std::filesystem::rename(replacement.staged, replacement.destination);
      replacement.installed = true;
    }
  } catch (...) {
    for (auto iterator = replacements.rbegin(); iterator != replacements.rend();
         ++iterator) {
      std::error_code ignored;
      if (iterator->installed) {
        std::filesystem::remove_all(iterator->destination, ignored);
      }
      if (iterator->had_previous) {
        std::filesystem::rename(iterator->backup, iterator->destination, ignored);
      }
    }
    throw;
  }
}

void progress(const HydrateConfig& config, std::string_view message) {
  if (config.progress) config.progress(message);
}

std::string fast_input_identity(const std::filesystem::path& input,
                                std::string_view manifest_hash,
                                std::string_view map_hash,
                                std::string_view toolchain_identity) {
  const auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::filesystem::last_write_time(input)
                                 .time_since_epoch())
                             .count();
  std::ostringstream value;
  value << std::filesystem::weakly_canonical(input).generic_string() << '\n'
        << std::filesystem::file_size(input) << '\n' << timestamp << '\n'
        << manifest_hash << '\n' << map_hash << '\n' << toolchain_identity;
  const auto text = value.str();
  return sha256_hex(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

std::string directory_identity(const std::filesystem::path& root) {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (entry.is_regular_file()) files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end(), [&](const auto& left, const auto& right) {
    return std::filesystem::relative(left, root).generic_string() <
           std::filesystem::relative(right, root).generic_string();
  });
  std::string contents;
  for (const auto& file : files) {
    contents += std::filesystem::relative(file, root).generic_string();
    contents.push_back('\0');
    contents += sha256_file(file);
    contents.push_back('\n');
  }
  return sha256_hex(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(contents.data()), contents.size()));
}

std::vector<std::string> cache_lines(const std::filesystem::path& path) {
  std::vector<std::string> result;
  if (!std::filesystem::is_regular_file(path)) return result;
  std::istringstream input(read_text(path));
  std::string line;
  while (std::getline(input, line)) result.push_back(std::move(line));
  return result;
}

} // namespace

ProjectManifest load_project_manifest(const std::filesystem::path& path) {
  ProjectManifest manifest;
  std::istringstream input(read_text(path));
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    const auto comment = line.find('#');
    if (comment != std::string::npos) line.resize(comment);
    const auto separator = line.find('=');
    if (separator == std::string::npos) {
      if (!trim(line).empty()) {
        throw std::runtime_error("invalid project.toml line " +
                                 std::to_string(line_number));
      }
      continue;
    }
    const auto key = trim(std::string_view(line).substr(0U, separator));
    const auto value = trim(std::string_view(line).substr(separator + 1U));
    if (key == "format_version") {
      try {
        manifest.format_version = static_cast<std::uint32_t>(std::stoul(value));
      } catch (const std::exception&) {
        throw std::runtime_error("invalid format_version in project.toml");
      }
    } else if (key == "display_name") {
      manifest.display_name = parse_string(value, line_number);
    } else if (key == "project_name") {
      manifest.project_name = parse_string(value, line_number);
    } else if (key == "disc_id") {
      manifest.disc_id = parse_string(value, line_number);
    } else if (key == "input_kind") {
      const auto kind = parse_string(value, line_number);
      if (kind == "iso") manifest.input_kind = InputKind::iso;
      else if (kind == "executable") manifest.input_kind = InputKind::executable;
      else throw std::runtime_error("unsupported input_kind in project.toml");
    } else if (key == "executable") {
      manifest.executable_path = parse_string(value, line_number);
    } else if (key == "sfo") {
      manifest.sfo_path = parse_string(value, line_number);
    } else if (key == "code_map") {
      manifest.code_map = parse_string(value, line_number);
    } else if (key == "psp_recompile_mode") {
      manifest.psp_recompile_mode = parse_string(value, line_number);
    } else if (key == "disc_extracted") {
      manifest.disc_extracted = parse_bool(value, line_number);
    } else if (key == "input_sha256") {
      manifest.input_sha256 = parse_string(value, line_number);
    } else if (key == "executable_sha256") {
      manifest.executable_sha256 = parse_string(value, line_number);
    }
  }
  if (manifest.format_version != 1U) {
    throw std::runtime_error("unsupported project.toml format_version: " +
                             std::to_string(manifest.format_version));
  }
  if (manifest.display_name.empty() || manifest.project_name.empty() ||
      manifest.executable_path.empty()) {
    throw std::runtime_error(
        "project.toml is missing display_name, project_name or executable");
  }
  return manifest;
}

HydrateSummary hydrate_codebase(const HydrateConfig& config) {
  const auto project =
      std::filesystem::absolute(config.project_directory).lexically_normal();
  const auto manifest_path = project / "project.toml";
  if (!std::filesystem::is_directory(project)) {
    throw std::runtime_error("project directory does not exist: " +
                             project.string());
  }
  if (!std::filesystem::is_directory(config.runtime_include_directory /
                                     "psprecomp")) {
    throw std::runtime_error("cannot find PSPRecomp runtime headers at: " +
                             config.runtime_include_directory.string());
  }
  if (!std::filesystem::is_directory(config.refract_directory)) {
    throw std::runtime_error("cannot find refract engine at: " +
                             config.refract_directory.string());
  }
  const auto manifest = load_project_manifest(manifest_path);
  const auto source = inspect_source(config.input);
  if (source.kind != manifest.input_kind) {
    throw std::runtime_error("input kind does not match project.toml");
  }
  if (!manifest.disc_id.empty() && source.disc_id != manifest.disc_id) {
    throw std::runtime_error("wrong game disc: expected " + manifest.disc_id +
                             ", found " +
                             (source.disc_id.empty() ? "no DISC_ID"
                                                     : source.disc_id));
  }

  const auto manifest_hash = sha256_file(manifest_path);
  std::string map_hash;
  if (!manifest.code_map.empty()) {
    const auto map_path = project / manifest.code_map;
    if (!std::filesystem::is_regular_file(map_path)) {
      throw std::runtime_error("project code map does not exist: " +
                               map_path.string());
    }
    map_hash = sha256_file(map_path);
  }
  std::string toolchain_identity = config.toolchain_revision;
  if (std::filesystem::is_regular_file(config.toolchain_executable)) {
    toolchain_identity += "\n" + sha256_file(config.toolchain_executable);
  }
  toolchain_identity +=
      "\n" + directory_identity(config.runtime_include_directory / "psprecomp") +
      "\n" + directory_identity(config.refract_directory);
  const auto fast_identity = fast_input_identity(
      config.input, manifest_hash, map_hash, toolchain_identity);
  const auto stamp_path = project / ".psprecomp/hydration.cache";
  const auto previous_cache = cache_lines(stamp_path);
  if (!config.force && previous_cache.size() == 3U &&
      previous_cache[0] == fast_identity &&
      hydration_outputs_exist(project, manifest.disc_extracted)) {
    return {true, previous_cache[1], previous_cache[2], 0U};
  }

  progress(config, "Hashing and validating the game input");
  const auto input_hash = sha256_file(config.input);
  if (!manifest.input_sha256.empty() && input_hash != manifest.input_sha256) {
    throw std::runtime_error(
        "unsupported game revision: input SHA-256 does not match project.toml");
  }
  const auto exported_marker =
      cache_lines(project / ".psprecomp/export-hydrated");
  if (!config.force && previous_cache.empty() && exported_marker.size() == 2U &&
      exported_marker[0] == input_hash &&
      exported_marker[1] == manifest.executable_sha256 &&
      hydration_outputs_exist(project, manifest.disc_extracted)) {
    std::filesystem::create_directories(stamp_path.parent_path());
    project_detail::write_text(stamp_path, fast_identity + "\n" + input_hash +
                                               "\n" + manifest.executable_sha256 +
                                               "\n");
    return {true, input_hash, manifest.executable_sha256, 0U};
  }
  const auto staging = unique_hydration_path(project);
  std::filesystem::create_directories(staging);
  try {
    std::vector<std::uint8_t> executable_data;
    if (source.kind == InputKind::iso) {
      const IsoImage iso(config.input);
      const auto executable = find_psp_executable(iso);
      if (!executable) {
        throw std::runtime_error("PSP executable disappeared from ISO");
      }
      executable_data = iso.read(*executable);
      if (manifest.disc_extracted) {
        progress(config, "Extracting the private disc filesystem");
        iso.extract_all(staging / "disc");
      }
    } else {
      executable_data = project_detail::read_binary(config.input);
    }
    if (is_encrypted_psp_data(executable_data)) {
      progress(config, "Decrypting the private PSP executable with PPSSPP");
      executable_data = decrypt_psp_executable(executable_data).bytes;
    } else if (!is_elf_data(executable_data)) {
      throw std::runtime_error("game input does not contain a supported PSP ELF");
    }
    const auto executable_hash = sha256_hex(executable_data);
    if (!manifest.executable_sha256.empty() &&
        executable_hash != manifest.executable_sha256) {
      throw std::runtime_error(
          "unsupported game revision: executable SHA-256 does not match "
          "project.toml");
    }
    const auto executable_path = staging / "decrypted.elf";
    project_detail::write_bytes(executable_path, executable_data);

    progress(config, "Loading the PSP executable");
    const auto elf = load_elf(executable_path);
    std::optional<CodeMap> map;
    if (!manifest.code_map.empty()) {
      map = load_code_map(project / manifest.code_map);
    }
    GeneratedProjectOptions options;
    options.display_name = manifest.display_name;
    options.module_name = manifest.project_name.substr(0U, 24U);
    options.target_name = manifest.project_name;
    options.include_path = "../../include";
    options.platform_directory = staging / "platform";
    progress(config, "Statically recompiling the game locally");
    emit_project(elf, staging / "src/generated", map ? &*map : nullptr,
                 options);

    progress(config, "Synchronizing the selected toolchain runtime");
    std::filesystem::create_directories(staging / "include");
    std::filesystem::copy(config.runtime_include_directory / "psprecomp",
                          staging / "include/psprecomp",
                          std::filesystem::copy_options::recursive);
    std::filesystem::copy(config.refract_directory, staging / "refract",
                          std::filesystem::copy_options::recursive);
    project_detail::write_text(staging / "src/generated/.gitkeep", "");
    project_detail::write_text(staging / "platform/.gitkeep", "");
    project_detail::write_text(staging / "include/psprecomp/.gitkeep", "");
    project_detail::write_text(staging / "refract/.gitkeep", "");
    if (manifest.disc_extracted) {
      project_detail::write_text(staging / "disc/.gitkeep", "");
    }

    std::vector<Replacement> replacements = {
        {staging / "src/generated", project / "src/generated",
         staging / "previous/src/generated"},
        {staging / "platform", project / "platform",
         staging / "previous/platform"},
        {staging / "include/psprecomp", project / "include/psprecomp",
         staging / "previous/include/psprecomp"},
        {staging / "refract", project / "refract",
         staging / "previous/refract"},
    };
    if (manifest.disc_extracted) {
      replacements.push_back({staging / "disc", project / "disc",
                              staging / "previous/disc"});
    }
    progress(config, "Publishing hydrated private files");
    publish_replacements(replacements);
    std::filesystem::create_directories(stamp_path.parent_path());
    project_detail::write_text(stamp_path, fast_identity + "\n" + input_hash +
                                               "\n" + executable_hash + "\n");

    std::size_t translation_units = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(project / "src/generated")) {
      if (entry.path().extension() == ".cpp") ++translation_units;
    }
    std::filesystem::remove_all(staging);
    return {false, input_hash, executable_hash, translation_units};
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove_all(staging, ignored);
    throw;
  }
}

} // namespace psprecomp
