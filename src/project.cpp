#include "project.hpp"

#include "decrypt.hpp"
#include "elf.hpp"
#include "emitter.hpp"
#include "iso.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace psprecomp {
namespace {

std::vector<std::uint8_t> read_binary(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream) {
    throw std::runtime_error("cannot open input: " + path.string());
  }
  const auto size = stream.tellg();
  if (size < 0) {
    throw std::runtime_error("cannot determine input size: " + path.string());
  }
  std::vector<std::uint8_t> result(static_cast<std::size_t>(size));
  stream.seekg(0);
  if (!result.empty()) {
    stream.read(reinterpret_cast<char *>(result.data()),
                static_cast<std::streamsize>(result.size()));
  }
  if (!stream) {
    throw std::runtime_error("failed while reading input: " + path.string());
  }
  return result;
}

bool has_psp_executable_magic(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  std::uint8_t magic[4]{};
  stream.read(reinterpret_cast<char *>(magic), sizeof(magic));
  if (stream.gcount() != 4) {
    return false;
  }
  const std::vector<std::uint8_t> prefix(std::begin(magic), std::end(magic));
  return is_elf_data(prefix) || is_encrypted_psp_data(prefix);
}

InputKind detect_kind(const std::filesystem::path &input) {
  if (!std::filesystem::is_regular_file(input)) {
    throw std::runtime_error("input does not exist or is not a file: " +
                             input.string());
  }
  if (has_psp_executable_magic(input)) {
    return InputKind::executable;
  }
  std::ifstream stream(input, std::ios::binary);
  stream.seekg(16LL * 2048LL + 1LL);
  char identifier[5]{};
  stream.read(identifier, sizeof(identifier));
  if (stream.gcount() == 5 && std::string_view(identifier, 5) == "CD001") {
    return InputKind::iso;
  }
  throw std::runtime_error(
      "unsupported input; expected a PSP ELF/PRX or ISO 9660 image");
}

void write_bytes(const std::filesystem::path &path,
                 const std::vector<std::uint8_t> &data) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    throw std::runtime_error("cannot create file: " + path.string());
  }
  if (!data.empty()) {
    stream.write(reinterpret_cast<const char *>(data.data()),
                 static_cast<std::streamsize>(data.size()));
  }
  if (!stream) {
    throw std::runtime_error("failed while writing file: " + path.string());
  }
}

void write_text(const std::filesystem::path &path, std::string_view value) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    throw std::runtime_error("cannot create file: " + path.string());
  }
  stream << value;
  if (!stream) {
    throw std::runtime_error("failed while writing file: " + path.string());
  }
}

std::string toml_string(std::string_view value) {
  std::string result{"\""};
  for (const auto character : value) {
    switch (character) {
    case '\\':
      result += "\\\\";
      break;
    case '"':
      result += "\\\"";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      break;
    default:
      result.push_back(character);
      break;
    }
  }
  result.push_back('"');
  return result;
}

std::string generated_readme(const ExportConfig &config, InputKind kind,
                             std::string_view executable_source) {
  std::ostringstream out;
  out << "# " << config.display_name
      << "\n\n"
         "This is a complete PSPRecomp export. It contains the generated C++ "
         "code, a vendored runtime, project metadata and build helpers.\n\n"
         "## Build\n\n"
         "Install PSPSDK and make sure `psp-config` is in `PATH`, then run:\n\n"
         "```sh\nmake ppsspp\n```\n\n"
         "This builds the game, prepares a lightweight PPSSPP run tree and "
         "launches it with the `ppsspp` command. Use `make -j` when you only "
         "want to build.\n\n"
         "The resulting `EBOOT.PBP` and PRX are written below "
         "`src/generated/`. Run `make disc-tree` to create a copy of the "
         "extracted disc with its executable replaced by the recompiled "
         "PRX.\n\n"
         "## Layout\n\n"
         "- `src/generated/`: generated functions, dispatcher and PSP entry\n"
         "- `include/psprecomp/`: portable runtime used by generated code\n"
         "- `config/`: the optional code map used for this export\n"
         "- `disc/`: extracted original disc filesystem (ISO inputs only)\n"
         "- `original/`: selected input executable when no disc was extracted\n"
         "- `project.toml`: reproducible project metadata\n\n"
         "Input type: `"
      << (kind == InputKind::iso ? "iso" : "executable") << "`  \n"
      << "Selected executable: `" << executable_source
      << "`\n\n"
         "> Keep copyrighted game data private and only work with dumps you "
         "are legally entitled to use.\n";
  return out.str();
}

std::string root_makefile(const ExportConfig &config, bool has_disc,
                          std::string_view disc_executable) {
  std::ostringstream out;
  out << "PPSSPP ?= ppsspp\n\n"
         ".PHONY: all clean rebuild ppsspp disc-tree help\n\n"
         "all:\n"
         "\t$(MAKE) -C src/generated\n\n"
         "clean:\n"
         "\t$(MAKE) -C src/generated clean\n\n"
         "rebuild: clean all\n\n";
  if (has_disc) {
    out << "ppsspp: all\n"
           "\tsh tools/prepare_ppsspp_run.sh\n"
           "\t$(PPSSPP) \"$(CURDIR)/.psprecomp/run\"\n\n"
           "disc-tree: all\n"
           "\trm -rf dist/disc\n"
           "\tmkdir -p dist\n"
           "\tcp -R disc dist/disc\n"
           "\tcp src/generated/"
        << config.project_name << ".prx dist/disc/" << disc_executable
        << "\n"
           "\tcp src/generated/PARAM.SFO dist/disc/PSP_GAME/PARAM.SFO\n"
           "\t@echo \"Prepared dist/disc with the recompiled executable.\"\n\n";
  } else {
    out << "ppsspp: all\n"
           "\t$(PPSSPP) \"$(CURDIR)/src/generated/EBOOT.PBP\"\n\n"
           "disc-tree:\n"
           "\t@echo \"disc-tree requires an ISO export.\"\n"
           "\t@false\n\n";
  }
  out << "help:\n"
         "\t@echo \"make          Build PRX and EBOOT.PBP\"\n"
         "\t@echo \"make ppsspp   Build and launch the game in PPSSPP\"\n"
         "\t@echo \"make clean    Remove compiler output\"\n"
         "\t@echo \"make disc-tree  Prepare a rebuilt disc tree\"\n";
  return out.str();
}

std::string ppsspp_run_script(const ExportConfig &config,
                              std::string_view disc_executable) {
  const auto executable_name =
      std::filesystem::path(disc_executable).filename().string();
  std::ostringstream out;
  out << "#!/bin/sh\n"
         "set -eu\n\n"
         "ROOT=$(CDPATH= cd -- \"$(dirname -- \"$0\")/..\" && pwd)\n"
         "DISC=$ROOT/disc\n"
         "STATE=$ROOT/.psprecomp\n"
         "RUN=$STATE/run\n\n"
         "if [ ! -d \"$DISC/PSP_GAME\" ]; then\n"
         "  echo \"psprecomp: extracted disc tree is missing\" >&2\n"
         "  exit 1\n"
         "fi\n"
         "if [ -e \"$RUN\" ] && [ ! -f \"$STATE/run.marker\" ]; then\n"
         "  echo \"psprecomp: refusing to replace an unowned run directory: "
         "$RUN\" >&2\n"
         "  exit 1\n"
         "fi\n\n"
         "rm -rf \"$RUN\"\n"
         "mkdir -p \"$RUN/PSP_GAME/SYSDIR\"\n"
         ": > \"$STATE/run.marker\"\n\n"
         "for source in \"$DISC\"/*; do\n"
         "  [ -e \"$source\" ] || continue\n"
         "  [ \"${source##*/}\" = PSP_GAME ] || ln -s \"$source\" "
         "\"$RUN/${source##*/}\"\n"
         "done\n"
         "for source in \"$DISC/PSP_GAME\"/*; do\n"
         "  [ -e \"$source\" ] || continue\n"
         "  case ${source##*/} in PARAM.SFO|SYSDIR) continue ;; esac\n"
         "  ln -s \"$source\" \"$RUN/PSP_GAME/${source##*/}\"\n"
         "done\n"
         "for source in \"$DISC/PSP_GAME/SYSDIR\"/*; do\n"
         "  [ -e \"$source\" ] || continue\n"
         "  [ \"${source##*/}\" = \""
      << executable_name
      << "\" ] || ln -s \"$source\" \"$RUN/PSP_GAME/SYSDIR/${source##*/}\"\n"
         "done\n\n"
         "cp \"$ROOT/src/generated/"
      << config.project_name << ".prx\" \"$RUN/"
      << std::filesystem::path(disc_executable).generic_string()
      << "\"\n"
         "cp \"$ROOT/src/generated/PARAM.SFO\" \"$RUN/PSP_GAME/PARAM.SFO\"\n"
         "echo \"Prepared lightweight PPSSPP run tree at $RUN\"\n";
  return out.str();
}

std::filesystem::path
unique_staging_path(const std::filesystem::path &destination) {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  return destination.parent_path() / ("." + destination.filename().string() +
                                      ".psprecomp-" + std::to_string(nonce));
}

} // namespace

SourceInfo inspect_source(const std::filesystem::path &input) {
  SourceInfo result;
  result.kind = detect_kind(input);
  if (result.kind == InputKind::executable) {
    result.suggested_display_name = input.stem().string();
    result.executable_path = input.filename().string();
    result.executable_encrypted = is_encrypted_psp_data(read_binary(input));
    return result;
  }
  const IsoImage image(input);
  const auto executable = find_psp_executable(image);
  if (!executable) {
    throw std::runtime_error(
        "ISO does not contain PSP_GAME/SYSDIR/EBOOT.BIN or BOOT.BIN");
  }
  const auto metadata = read_psp_disc_metadata(image);
  result.suggested_display_name = metadata.title;
  result.disc_id = metadata.disc_id;
  result.executable_path = executable->path.generic_string();
  result.executable_encrypted = is_encrypted_psp_data(image.read(*executable));
  result.disc_entries = image.entries().size();
  return result;
}

std::string project_slug(std::string_view value) {
  std::string result;
  bool separator = false;
  for (const auto character : value) {
    const auto byte = static_cast<unsigned char>(character);
    if (std::isalnum(byte)) {
      if (separator && !result.empty()) {
        result.push_back('_');
      }
      result.push_back(static_cast<char>(std::tolower(byte)));
      separator = false;
    } else {
      separator = true;
    }
  }
  if (result.empty()) {
    result = "psp_recompiled";
  }
  if (std::isdigit(static_cast<unsigned char>(result.front()))) {
    result.insert(0, "game_");
  }
  return result;
}

ExportSummary export_codebase(const ExportConfig &config) {
  if (config.display_name.empty() || config.project_name.empty()) {
    throw std::runtime_error("display name and project name cannot be empty");
  }
  if (std::filesystem::exists(config.output_directory)) {
    throw std::runtime_error("output already exists: " +
                             config.output_directory.string());
  }
  const auto runtime_source = config.runtime_include_directory / "psprecomp";
  if (!std::filesystem::is_directory(runtime_source)) {
    throw std::runtime_error("cannot find PSPRecomp runtime headers at: " +
                             runtime_source.string());
  }

  const auto info = inspect_source(config.input);
  auto staging = unique_staging_path(config.output_directory);
  std::filesystem::create_directories(config.output_directory.parent_path());
  std::filesystem::create_directories(staging);
  try {
    std::filesystem::path executable;
    std::string decryption_backend;
    if (info.kind == InputKind::iso) {
      const IsoImage iso(config.input);
      const auto iso_executable = find_psp_executable(iso);
      if (!iso_executable) {
        throw std::runtime_error("PSP executable disappeared from ISO");
      }
      const auto executable_data = iso.read(*iso_executable);
      if (is_encrypted_psp_data(executable_data)) {
        if (config.progress) {
          config.progress("Decrypting the ~PSP executable with PPSSPP");
        }
        auto decrypted = decrypt_psp_executable(executable_data);
        decryption_backend = std::move(decrypted.backend);
        executable = staging / "original" / "decrypted.elf";
        write_bytes(executable, decrypted.bytes);
      } else if (!is_elf_data(executable_data)) {
        throw std::runtime_error(
            "PSP_GAME/SYSDIR contains neither an ELF nor a supported ~PSP "
            "encrypted executable");
      }
      if (config.extract_disc) {
        std::uintmax_t extracted_size = 0;
        for (const auto &entry : iso.entries()) {
          if (!entry.directory) {
            extracted_size += entry.size;
          }
        }
        const auto space = std::filesystem::space(staging);
        if (extracted_size > space.available) {
          throw std::runtime_error(
              "not enough free space to extract the ISO (requires " +
              std::to_string(extracted_size) + " bytes)");
        }
        if (config.progress) {
          config.progress("Extracting the disc filesystem");
        }
        iso.extract_all(staging / "disc");
        if (executable.empty()) {
          executable = staging / "disc" / iso_executable->path;
        }
      } else if (executable.empty()) {
        executable = staging / "original" / iso_executable->path.filename();
        write_bytes(executable, executable_data);
      }
    } else {
      const auto executable_data = read_binary(config.input);
      if (is_encrypted_psp_data(executable_data)) {
        if (config.progress) {
          config.progress("Decrypting the ~PSP executable with PPSSPP");
        }
        auto decrypted = decrypt_psp_executable(executable_data);
        decryption_backend = std::move(decrypted.backend);
        executable = staging / "original" / "decrypted.elf";
        write_bytes(executable, decrypted.bytes);
      } else {
        executable = staging / "original" / config.input.filename();
        std::filesystem::create_directories(executable.parent_path());
        std::filesystem::copy_file(config.input, executable);
      }
    }

    if (config.progress) {
      config.progress("Loading and validating the PSP executable");
    }
    ElfImage elf;
    try {
      elf = load_elf(executable);
    } catch (const std::exception &error) {
      throw std::runtime_error(
          std::string("selected PSP executable cannot be recompiled: ") +
          error.what());
    }
    std::optional<CodeMap> map;
    if (config.code_map) {
      if (config.progress) {
        config.progress("Loading function metadata");
      }
      map = load_code_map(*config.code_map);
      std::filesystem::create_directories(staging / "config");
      std::filesystem::copy_file(*config.code_map,
                                 staging / "config" / "code.map");
    }

    std::filesystem::create_directories(staging / "include");
    std::filesystem::copy(runtime_source, staging / "include" / "psprecomp",
                          std::filesystem::copy_options::recursive);
    GeneratedProjectOptions emitter_options;
    emitter_options.display_name = config.display_name;
    emitter_options.module_name = config.project_name.substr(0, 27U);
    emitter_options.target_name = config.project_name;
    emitter_options.include_path = "../../include";
    if (config.progress) {
      config.progress("Translating Allegrex code to C++");
    }
    emit_project(elf, staging / "src" / "generated", map ? &*map : nullptr,
                 emitter_options);

    if (config.progress) {
      config.progress("Writing project files");
    }
    const bool has_disc = info.kind == InputKind::iso && config.extract_disc;
    write_text(staging / "Makefile",
               root_makefile(config, has_disc, info.executable_path));
    if (has_disc) {
      write_text(staging / "tools" / "prepare_ppsspp_run.sh",
                 ppsspp_run_script(config, info.executable_path));
    }
    write_text(staging / ".gitignore",
               "src/generated/*.o\nsrc/generated/*.elf\n"
               "src/generated/*.prx\nsrc/generated/*.PBP\n"
               "src/generated/PARAM.SFO\nsrc/generated/SND0.AT3\n"
               "disc/\noriginal/\ndist/\n.psprecomp/\n.DS_Store\n");
    write_text(
        staging / "README.md",
        generated_readme(
            config, info.kind,
            std::filesystem::relative(executable, staging).generic_string()));
    std::ostringstream manifest;
    manifest << "format_version = 1\n"
                "display_name = "
             << toml_string(config.display_name)
             << "\nproject_name = " << toml_string(config.project_name)
             << "\ndisc_id = " << toml_string(config.disc_id)
             << "\ninput_kind = "
             << toml_string(info.kind == InputKind::iso ? "iso" : "executable")
             << "\nexecutable = " << toml_string(info.executable_path)
             << "\ndecryption_backend = " << toml_string(decryption_backend)
             << "\ncode_map = "
             << toml_string(config.code_map ? "config/code.map" : "")
             << "\ndisc_extracted = " << (has_disc ? "true" : "false") << "\n";
    write_text(staging / "project.toml", manifest.str());

    std::size_t translation_units = 0;
    for (const auto &entry :
         std::filesystem::directory_iterator(staging / "src" / "generated")) {
      if (entry.path().extension() == ".cpp") {
        ++translation_units;
      }
    }
    if (config.progress) {
      config.progress("Publishing the completed project");
    }
    std::filesystem::rename(staging, config.output_directory);
    return {info.kind,          config.output_directory, info.executable_path,
            decryption_backend, info.disc_entries,       translation_units};
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove_all(staging, ignored);
    throw;
  }
}

} // namespace psprecomp
