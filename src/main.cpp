#include "elf.hpp"
#include "emitter.hpp"
#include "iso_patch.hpp"
#include "project.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

void usage(const char *program) {
  std::cout
      << "PSPRecomp - PSP static recompilation toolkit\n\n"
         "Beginner-friendly project export:\n"
         "  "
      << program
      << "                         Start the interactive wizard\n"
         "  "
      << program
      << " <game.iso|input.elf>    Inspect input and start the wizard\n"
         "  "
      << program
      << " init [input] [options]  Export a complete codebase\n"
         "  "
      << program
      << " hydrate [options]       Generate private files from the user's game\n\n"
         "Init options:\n"
         "  --display-name <name>   Name shown in the PSP menu\n"
         "  --project-name <name>   Filesystem/build-safe project name\n"
         "  --output <directory>    Destination directory\n"
         "  --code-map <file>       Optional Ghidra function/code map\n"
         "  --extract-disc          Extract all ISO files (default)\n"
         "  --no-extract-disc       Only extract the selected executable\n"
         "  -y, --yes                Accept defaults; do not prompt\n\n"
         "Hydrate options:\n"
         "  --project <directory>  Existing exported project (default: .)\n"
         "  --input <game.iso>     User-owned ISO/ELF (default: original/disc.iso)\n"
         "  --force                Regenerate even when the hydration cache matches\n\n"
         "Low-level compatibility commands:\n"
         "  "
      << program
      << " <input.elf> --analyze [--code-map <map>]\n"
         "  "
      << program
      << " <input.elf> -o <output.cpp> [--code-map <map>]\n"
         "  "
      << program
      << " <input.elf> --output-dir <directory> [--code-map <map>]\n";
}

bool terminal_is_interactive() {
#if defined(_WIN32)
  return ::_isatty(::_fileno(stdin)) != 0 && ::_isatty(::_fileno(stdout)) != 0;
#else
  return ::isatty(STDIN_FILENO) != 0 && ::isatty(STDOUT_FILENO) != 0;
#endif
}

std::string prompt(std::string_view label, std::string_view default_value = {},
                   bool required = true) {
  while (true) {
    std::cout << "  " << label;
    if (!default_value.empty()) {
      std::cout << " [" << default_value << "]";
    }
    std::cout << ": " << std::flush;
    std::string value;
    if (!std::getline(std::cin, value)) {
      throw std::runtime_error("interactive input was closed");
    }
    if (value.empty()) {
      value = std::string(default_value);
    }
    if (!required || !value.empty()) {
      return value;
    }
    std::cout << "    Please enter a value.\n";
  }
}

bool prompt_yes_no(std::string_view label, bool default_value) {
  while (true) {
    std::cout << "  " << label << (default_value ? " [Y/n]: " : " [y/N]: ")
              << std::flush;
    std::string value;
    if (!std::getline(std::cin, value)) {
      throw std::runtime_error("interactive input was closed");
    }
    if (value.empty()) {
      return default_value;
    }
    if (value == "y" || value == "Y" || value == "yes" || value == "Yes") {
      return true;
    }
    if (value == "n" || value == "N" || value == "no" || value == "No") {
      return false;
    }
    std::cout << "    Please answer y or n.\n";
  }
}

struct InitArguments {
  std::filesystem::path input;
  std::filesystem::path output;
  std::optional<std::filesystem::path> code_map;
  std::string display_name;
  std::string project_name;
  bool extract_disc{true};
  bool extract_disc_set{};
  bool assume_yes{};
};

struct HydrateArguments {
  std::filesystem::path project{"."};
  std::filesystem::path input;
  bool force{};
};

std::filesystem::path resolve_executable(std::string_view program) {
  const std::filesystem::path supplied(program);
  if (supplied.has_parent_path()) {
    return std::filesystem::absolute(supplied).lexically_normal();
  }
  const auto* path_environment = std::getenv("PATH");
  if (path_environment == nullptr) return {};
  std::string_view paths(path_environment);
  constexpr char path_separator =
#if defined(_WIN32)
      ';';
#else
      ':';
#endif
  while (!paths.empty()) {
    const auto separator = paths.find(path_separator);
    const auto directory = paths.substr(0U, separator);
    const auto candidate = std::filesystem::path(directory) / supplied;
    if (std::filesystem::is_regular_file(candidate)) {
      return std::filesystem::absolute(candidate).lexically_normal();
    }
    if (separator == std::string_view::npos) break;
    paths.remove_prefix(separator + 1U);
  }
  return {};
}

HydrateArguments parse_hydrate_arguments(int argc, char** argv, int first) {
  HydrateArguments result;
  for (int index = first; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    const auto take_value = [&](std::string_view option) -> std::string {
      if (index + 1 >= argc) {
        throw std::runtime_error("missing value after " + std::string(option));
      }
      return argv[++index];
    };
    if (argument == "--project") {
      result.project = take_value(argument);
    } else if (argument == "--input") {
      result.input = take_value(argument);
    } else if (argument == "--force") {
      result.force = true;
    } else {
      throw std::runtime_error("unknown hydrate option: " +
                               std::string(argument));
    }
  }
  return result;
}

int run_hydrate(HydrateArguments arguments, std::string_view executable) {
  arguments.project =
      std::filesystem::absolute(arguments.project).lexically_normal();
  if (arguments.input.empty()) {
    arguments.input = arguments.project / "original/disc.iso";
  }
  arguments.input =
      std::filesystem::absolute(arguments.input).lexically_normal();
  if (!std::filesystem::is_regular_file(arguments.input)) {
    throw std::runtime_error(
        "game input not found: " + arguments.input.string() +
        "\nPlace your legally obtained ISO at original/disc.iso or pass "
        "--input <path>.");
  }

  psprecomp::HydrateConfig config;
  config.input = arguments.input;
  config.project_directory = arguments.project;
  config.runtime_include_directory = PSPRECOMP_SOURCE_INCLUDE_DIR;
  config.refract_directory = REFRACT_REFRACT_DIR;
  config.toolchain_executable = resolve_executable(executable);
#ifdef PSPRECOMP_BUILD_REVISION
  config.toolchain_revision = PSPRECOMP_BUILD_REVISION;
#else
  config.toolchain_revision = "unknown";
#endif
  config.force = arguments.force;
  config.progress = [](std::string_view message) {
    std::cout << "  -> " << message << " ...\n";
  };
  const auto summary = psprecomp::hydrate_codebase(config);
  if (summary.cached) {
    std::cout << "Hydration is current; using cached private files.\n";
  } else {
    std::cout << "Hydrated project with " << summary.generated_translation_units
              << " generated C++ translation units.\n";
  }
  return 0;
}

InitArguments parse_init_arguments(int argc, char **argv, int first) {
  InitArguments result;
  for (int i = first; i < argc; ++i) {
    const std::string_view argument(argv[i]);
    const auto take_value = [&](std::string_view option) -> std::string {
      if (i + 1 >= argc) {
        throw std::runtime_error("missing value after " + std::string(option));
      }
      return argv[++i];
    };
    if (argument == "--display-name") {
      result.display_name = take_value(argument);
    } else if (argument == "--project-name") {
      result.project_name = take_value(argument);
    } else if (argument == "--output" || argument == "-o") {
      result.output = take_value(argument);
    } else if (argument == "--code-map") {
      result.code_map = take_value(argument);
    } else if (argument == "--extract-disc") {
      result.extract_disc = true;
      result.extract_disc_set = true;
    } else if (argument == "--no-extract-disc") {
      result.extract_disc = false;
      result.extract_disc_set = true;
    } else if (argument == "--yes" || argument == "-y") {
      result.assume_yes = true;
    } else if (!argument.empty() && argument.front() != '-' &&
               result.input.empty()) {
      result.input = argv[i];
    } else {
      throw std::runtime_error("unknown init option: " + std::string(argument));
    }
  }
  return result;
}

int run_init(InitArguments arguments) {
  const bool interactive = terminal_is_interactive() && !arguments.assume_yes;
  if (interactive) {
    std::cout
        << "\nPSPRecomp project wizard\n"
           "-------------------------\n"
           "Turn a PSP executable or ISO into a buildable C++ project.\n\n";
  }
  if (arguments.input.empty()) {
    if (!interactive) {
      throw std::runtime_error("an input file is required without a terminal");
    }
    arguments.input = prompt("PSP ISO, ELF or PRX");
  }
  arguments.input =
      std::filesystem::absolute(arguments.input).lexically_normal();

  if (interactive) {
    std::cout << "\nInspecting " << arguments.input.string() << " ...\n";
  }
  const auto source = psprecomp::inspect_source(arguments.input);
  auto suggested_name = source.suggested_display_name;
  if (suggested_name.empty()) {
    suggested_name = !source.disc_id.empty() ? source.disc_id
                                             : arguments.input.stem().string();
  }
  if (suggested_name.empty()) {
    suggested_name = "PSP Recompiled";
  }
  if (arguments.display_name.empty()) {
    arguments.display_name =
        interactive ? prompt("Display name", suggested_name) : suggested_name;
  }
  if (arguments.project_name.empty()) {
    const auto suggested_slug = psprecomp::project_slug(arguments.display_name);
    arguments.project_name =
        interactive ? prompt("Project name", suggested_slug) : suggested_slug;
  }
  arguments.project_name = psprecomp::project_slug(arguments.project_name);
  if (arguments.output.empty()) {
    const auto suggested_output =
        (std::filesystem::current_path() / arguments.project_name).string();
    arguments.output = interactive
                           ? prompt("Output directory", suggested_output)
                           : suggested_output;
  }
  arguments.output =
      std::filesystem::absolute(arguments.output).lexically_normal();

  if (interactive && !arguments.code_map) {
    const auto value = prompt("Code map (optional)", {}, false);
    if (!value.empty()) {
      arguments.code_map = value;
    }
  }
  if (arguments.code_map) {
    *arguments.code_map =
        std::filesystem::absolute(*arguments.code_map).lexically_normal();
    if (!std::filesystem::is_regular_file(*arguments.code_map)) {
      throw std::runtime_error("code map does not exist: " +
                               arguments.code_map->string());
    }
  }
  if (interactive && source.kind == psprecomp::InputKind::iso &&
      !arguments.extract_disc_set) {
    arguments.extract_disc =
        prompt_yes_no("Extract the complete disc filesystem", true);
  }

  if (interactive) {
    std::cout << "\nReady to export\n"
              << "  Input:        " << arguments.input.string() << '\n'
              << "  Executable:   " << source.executable_path
              << (source.executable_encrypted
                      ? " (~PSP encrypted; decrypt automatically)"
                      : "")
              << '\n'
              << "  Display name: " << arguments.display_name << '\n'
              << "  Project name: " << arguments.project_name << '\n'
              << "  Output:       " << arguments.output.string() << '\n'
              << "  Code map:     "
              << (arguments.code_map ? arguments.code_map->string() : "none")
              << '\n';
    if (source.kind == psprecomp::InputKind::iso) {
      std::cout << "  Disc files:   " << source.disc_entries << " ("
                << (arguments.extract_disc ? "extract all" : "executable only")
                << ")\n";
    }
    std::cout << '\n';
    if (!prompt_yes_no("Create this project", true)) {
      std::cout << "Cancelled. Nothing was written.\n";
      return 0;
    }
    std::cout << "\nGenerating codebase ...\n";
  }

  psprecomp::ExportConfig config;
  config.input = arguments.input;
  config.output_directory = arguments.output;
  config.runtime_include_directory = PSPRECOMP_SOURCE_INCLUDE_DIR;
  config.refract_directory = REFRACT_REFRACT_DIR;
  config.code_map = arguments.code_map;
  config.display_name = arguments.display_name;
  config.project_name = arguments.project_name;
  config.disc_id = source.disc_id;
  config.extract_disc = arguments.extract_disc;
  if (interactive) {
    config.progress = [](std::string_view message) {
      std::cout << "  -> " << message << " ...\n";
    };
  }
  const auto summary = psprecomp::export_codebase(config);

  std::cout << "\nCreated " << summary.output_directory.string() << '\n'
            << "  " << summary.generated_translation_units
            << " generated C++ translation units\n";
  if (summary.input_kind == psprecomp::InputKind::iso &&
      arguments.extract_disc) {
    std::cout << "  " << summary.disc_entries
              << " ISO entries exported to disc/\n";
  }
  if (!summary.decryption_backend.empty()) {
    std::cout << "  Decrypted executable with " << summary.decryption_backend
              << '\n';
  }
  std::cout << "\nNext steps:\n"
            << "  cd \"" << summary.output_directory.string() << "\"\n"
            << "  make psp-run\n";
  return 0;
}

int run_patch_iso(int argc, char **argv) {
  // psprism patch-iso <original.iso> <output.iso> <iso_path>=<local_file>...
  if (argc < 5) {
    throw std::runtime_error(
        "usage: patch-iso <original.iso> <output.iso> "
        "<iso_path>=<local_file> [...]");
  }
  const std::filesystem::path original = argv[2];
  const std::filesystem::path output = argv[3];
  std::vector<psprecomp::IsoPatchReplacement> replacements;
  for (int i = 4; i < argc; ++i) {
    const std::string_view argument(argv[i]);
    const auto separator = argument.find('=');
    if (separator == std::string_view::npos) {
      throw std::runtime_error(
          "expected <iso_path>=<local_file>, got: " + std::string(argument));
    }
    replacements.push_back({std::string(argument.substr(0, separator)),
                            std::filesystem::path(
                                argument.substr(separator + 1U))});
  }
  psprecomp::patch_iso_preserving_layout(original, output, replacements);
  std::cout << "Patched ISO written to " << output.string() << '\n';
  return 0;
}

int run_legacy(int argc, char **argv) {
  if (argc < 3) {
    throw std::runtime_error("missing output mode");
  }
  std::filesystem::path output;
  std::filesystem::path map_path;
  std::filesystem::path output_directory;
  bool analyze = false;
  for (int i = 2; i < argc; ++i) {
    const std::string_view argument(argv[i]);
    if (argument == "--analyze") {
      analyze = true;
    } else if ((argument == "-o" || argument == "--code-map" ||
                argument == "--output-dir") &&
               i + 1 < argc) {
      if (argument == "-o") {
        output = argv[++i];
      } else if (argument == "--code-map") {
        map_path = argv[++i];
      } else {
        output_directory = argv[++i];
      }
    } else {
      throw std::runtime_error("unknown option: " + std::string(argument));
    }
  }
  const auto output_modes = static_cast<int>(analyze) +
                            static_cast<int>(!output.empty()) +
                            static_cast<int>(!output_directory.empty());
  if (output_modes != 1) {
    throw std::runtime_error("choose exactly one output mode");
  }
  const auto image = psprecomp::load_elf(argv[1]);
  const auto code_map = map_path.empty() ? psprecomp::CodeMap{}
                                         : psprecomp::load_code_map(map_path);
  const auto *code_map_ptr = map_path.empty() ? nullptr : &code_map;
  if (analyze) {
    std::cout << "load_segments=" << image.load_segments.size() << '\n'
              << "memory_size=" << image.memory_size() << '\n'
              << "relocations=" << image.relocations.size() << '\n'
              << "imports=" << image.imports.size() << '\n';
    return psprecomp::analyze_coverage(image, code_map_ptr, std::cout) ? 0 : 1;
  }
  if (!output_directory.empty()) {
    psprecomp::emit_project(image, output_directory, code_map_ptr);
    std::cout << "Generated project: " << output_directory.string() << " ("
              << image.memory_size() << " guest bytes, "
              << image.relocations.size() << " relocations, "
              << image.imports.size() << " imports)\n";
    return 0;
  }
  psprecomp::emit_cpp(image, output, code_map_ptr);
  std::cout << "Translated " << image.executable_sections.size()
            << " executable section(s), entry 0x" << std::hex << image.entry
            << " -> " << output.string() << '\n';
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc == 2 && (std::string_view(argv[1]) == "--help" ||
                      std::string_view(argv[1]) == "-h")) {
      usage(argv[0]);
      return 0;
    }
    if (argc == 1) {
      return run_init({});
    }
    if (std::string_view(argv[1]) == "init") {
      return run_init(parse_init_arguments(argc, argv, 2));
    }
    if (std::string_view(argv[1]) == "hydrate") {
      return run_hydrate(parse_hydrate_arguments(argc, argv, 2), argv[0]);
    }
    if (std::string_view(argv[1]) == "patch-iso") {
      return run_patch_iso(argc, argv);
    }
    if (argc == 2) {
      InitArguments arguments;
      arguments.input = argv[1];
      return run_init(std::move(arguments));
    }
    return run_legacy(argc, argv);
  } catch (const std::exception &error) {
    std::cerr << "psprecomp: " << error.what() << '\n';
    return 1;
  }
}
