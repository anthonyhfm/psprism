#include "elf.hpp"
#include "emitter.hpp"
#include "project.hpp"

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
      << " init [input] [options]  Export a complete codebase\n\n"
         "Init options:\n"
         "  --display-name <name>   Name shown in the PSP menu\n"
         "  --project-name <name>   Filesystem/build-safe project name\n"
         "  --output <directory>    Destination directory\n"
         "  --code-map <file>       Optional Ghidra function/code map\n"
         "  --extract-disc          Extract all ISO files (default)\n"
         "  --no-extract-disc       Only extract the selected executable\n"
         "  -y, --yes                Accept defaults; do not prompt\n\n"
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
            << "  make -j\n";
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
