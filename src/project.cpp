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

std::vector<std::uint8_t> read_binary(const std::filesystem::path& path) {
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
    stream.read(reinterpret_cast<char*>(result.data()),
                static_cast<std::streamsize>(result.size()));
  }
  if (!stream) {
    throw std::runtime_error("failed while reading input: " + path.string());
  }
  return result;
}

bool has_psp_executable_magic(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  std::uint8_t magic[4]{};
  stream.read(reinterpret_cast<char*>(magic), sizeof(magic));
  if (stream.gcount() != 4) {
    return false;
  }
  const std::vector<std::uint8_t> prefix(std::begin(magic), std::end(magic));
  return is_elf_data(prefix) || is_encrypted_psp_data(prefix);
}

InputKind detect_kind(const std::filesystem::path& input) {
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

void write_bytes(const std::filesystem::path& path,
                 const std::vector<std::uint8_t>& data) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    throw std::runtime_error("cannot create file: " + path.string());
  }
  if (!data.empty()) {
    stream.write(reinterpret_cast<const char*>(data.data()),
                 static_cast<std::streamsize>(data.size()));
  }
  if (!stream) {
    throw std::runtime_error("failed while writing file: " + path.string());
  }
}

void write_text(const std::filesystem::path& path, std::string_view value) {
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

std::string generated_readme(const ExportConfig& config, InputKind kind,
                             std::string_view executable_source) {
  std::ostringstream out;
  out << "# " << config.display_name
      << "\n\n"
         "This is a complete PSPRecomp export. It contains the generated C++ "
         "code, a vendored runtime, project metadata and build helpers.\n\n"
         "## Build\n\n"
         "The generated codebase has one command per target platform:\n\n"
         "```sh\n"
         "make psp        # build PSP output (plus ISO for disc exports)\n"
         "make psp-run    # build and launch through the PPSSPP CLI\n"
         "make macos      # build a native Release .app (-O3)\n"
         "make macos-debug # build a native Debug .app\n"
         "make macos-run  # build and run the native Release app\n"
         "```\n\n"
         "The PSP targets require PSPSDK and `psp-config` in `PATH`. The "
         "macOS target requires CMake, Apple Clang and Qt 6 base for desktop "
         "system dialogs. Homebrew users can install it with "
         "`brew install qtbase`.\n\n"
         "## Controls\n\n"
         "Native macOS builds support standard game controllers and a "
         "keyboard fallback: arrows = D-pad, WASD = analog stick, I/J/K/L = "
         "Triangle/Square/Cross/Circle, Q/E = L/R, Return = Start and Right "
         "Shift = Select.\n\n"
         "The resulting `EBOOT.PBP` and PRX are written below "
         "`src/generated/`. Run `make disc-tree` to create a copy of the "
         "extracted disc with its executable replaced by the recompiled "
         "PRX.\n\n"
         "## Layout\n\n"
         "- `src/generated/`: platform-independent functions and dispatcher\n"
         "- `platform/platform.h`: complete imported-API contract\n"
         "- `platform/psp/`: PSP entry point, ABI bridge and real SCE calls\n"
         "- `platform/macos/`: native entry point and psprism adapter\n"
         "- `refract/`: vendored, game-editable PSP-to-host runtime engine\n"
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

std::string root_makefile(const ExportConfig& config, bool has_disc,
                          std::string_view disc_executable,
                          std::string_view sfo_path,
                          std::string_view psp_recompile_mode) {
  std::ostringstream out;
  out << "PPSSPP ?= ppsspp\n"
         "CMAKE ?= cmake\n"
         "CXX ?= c++\n"
         "PSP_RECOMPILE_MODE := "
      << psp_recompile_mode
      << "\n"
         "MACOS_BUILD_TYPE ?= Release\n"
         "MACOS_RUN_ARGS ?=\n"
         "\n"
         ".PHONY: all psp-recompile-check psp-binary psp psp-run macos "
         "macos-debug macos-run "
         "clean rebuild "
         "ppsspp "
         "disc-tree help\n\n"
         "all: psp\n\n"
         "psp-recompile-check:\n"
         "\t@echo \"PSP recompile mode: $(PSP_RECOMPILE_MODE)\"\n\n"
         "psp-binary: psp-recompile-check\n"
         "\t$(MAKE) -C src/generated\n\n";
  if (has_disc) {
    out << "psp: psp-binary\n"
        << "\tmkdir -p dist .psprecomp\n"
           "\t$(CXX) -std=c++20 -O2 -o .psprecomp/iso_patch "
           "tools/iso_patch.cpp\n"
           "\t.psprecomp/iso_patch \"$(CURDIR)/original/disc.iso\" "
           "\"$(CURDIR)/dist/"
        << config.project_name << ".iso\""
        << " \"" + std::string(disc_executable) +
               "\"=\"$(CURDIR)/src/generated/" + config.project_name +
               ".prx\"" +
               (!sfo_path.empty()
                    ? " \"" + std::string(sfo_path) +
                          "\"=\"$(CURDIR)/src/generated/PARAM.SFO\"\n"
                    : "\n")
        << "\n"
           "psp-run: psp\n"
           "\t$(PPSSPP) \"$(CURDIR)/dist/"
        << config.project_name << ".iso\"\n\n";
  } else {
    out << "psp: psp-binary\n"
           "\t@echo \"Built src/generated/EBOOT.PBP (an ISO input is "
           "required to create a disc image).\"\n\n"
           "psp-run: psp\n"
           "\t$(PPSSPP) \"$(CURDIR)/src/generated/EBOOT.PBP\"\n\n";
  }
  out << "macos:\n"
         "\t$(CMAKE) -S . -B build/macos "
         "-DCMAKE_BUILD_TYPE=$(MACOS_BUILD_TYPE)\n"
         "\t$(CMAKE) --build build/macos -j\n\n"
         "macos-debug:\n"
         "\t$(MAKE) macos MACOS_BUILD_TYPE=Debug\n\n"
         "macos-run: macos\n"
         "\tREFRACT_DISC_ROOT=\"$(CURDIR)/disc\" "
         "REFRACT_DISC_IMAGE=\"$(CURDIR)/original/disc.iso\" "
         "REFRACT_WRITABLE_ROOT=\"$(CURDIR)/.refract/ms0\" "
         "\"$(CURDIR)/build/macos/"
      << config.project_name << ".app/Contents/MacOS/" << config.project_name
      << "\" $(MACOS_RUN_ARGS)\n\n"
         "ppsspp: psp-run\n\n"
         "clean:\n"
         "\t$(MAKE) -C src/generated clean\n"
         "\t$(CMAKE) -E rm -rf build/macos\n\n"
         "rebuild: clean all\n\n";
  if (has_disc) {
    out << "disc-tree: psp-binary\n"
           "\trm -rf dist/disc\n"
           "\tmkdir -p dist\n"
           "\tcp -R disc dist/disc\n"
        << "\tcp src/generated/" + config.project_name +
               ".prx dist/disc/" + std::string(disc_executable) +
               (!sfo_path.empty()
                    ? "\n\tcp src/generated/PARAM.SFO dist/disc/" +
                          std::string(sfo_path) + "\n"
                    : "\n") +
               "\t@echo \"Prepared dist/disc with the recompiled executable ($(PSP_RECOMPILE_MODE)).\"\n\n";
  } else {
    out << "disc-tree:\n"
           "\t@echo \"disc-tree requires an ISO export.\"\n"
           "\t@false\n\n";
  }
  out << "help:\n"
         "\t@echo \"make psp        Build the PSP PRX and EBOOT"
      << (has_disc ? " plus ISO" : "")
      << "\"\n"
         "\t@echo \"make psp-run    Build PSP and launch it in PPSSPP\"\n"
         "\t@echo \"make macos      Build a native Release .app (-O3)\"\n"
         "\t@echo \"make macos-debug Build a native Debug .app\"\n"
         "\t@echo \"make macos-run  Build and launch the Release .app\"\n"
         "\t@echo \"make clean      Remove compiler output\"\n";
  return out.str();
}

//
// Source for a small, dependency-free host tool that rebuilds the PSP disc
// image while preserving the logical block address (LBA) of every file that
// is not being replaced.
//
// A naive from-scratch ISO repack (mkisofs/xorriso/hdiutil on the extracted
// tree) lays every file out again using its own ordering rules. Several PSP
// games (observed with Need for Speed: Most Wanted) resolve large assets
// through raw sector addressing baked into their own code
// (`disc0:/sce_lbnXXXXX_sizeYYY`), bypassing the filesystem entirely. A
// repack silently relocates that data, so those games load garbage/corrupted
// modules instead of the real ones and crash or hang instead of booting.
// Copying the original image and only relocating the handful of files that
// changed (patching just their directory record) keeps every untouched
// asset's LBA byte-identical to the retail disc.
std::string iso_patch_tool_source() {
  return R"CPP(// Generated by psprism: rebuilds a PSP disc image while preserving the
// logical block address (LBA) of every file that is not explicitly replaced.
// See src/project.cpp (iso_patch_tool_source) in the psprism repository for
// the full rationale.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t sector_size = 2048U;

struct Replacement {
  std::string iso_path;
  std::filesystem::path local_path;
};

std::string normalized_path(std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const auto character : value) {
    if (character == '\\') {
      result.push_back('/');
    } else if (character != '/') {
      result.push_back(static_cast<char>(
          std::toupper(static_cast<unsigned char>(character))));
    } else if (!result.empty() && result.back() != '/') {
      result.push_back('/');
    }
  }
  while (!result.empty() && result.front() == '/') {
    result.erase(result.begin());
  }
  while (!result.empty() && result.back() == '/') {
    result.pop_back();
  }
  return result;
}

std::uint32_t read_u32(const std::vector<std::uint8_t> &data,
                       std::size_t offset) {
  if (offset > data.size() || data.size() - offset < 4U) {
    throw std::runtime_error("truncated ISO 9660 field");
  }
  return static_cast<std::uint32_t>(data[offset]) |
         static_cast<std::uint32_t>(data[offset + 1U]) << 8U |
         static_cast<std::uint32_t>(data[offset + 2U]) << 16U |
         static_cast<std::uint32_t>(data[offset + 3U]) << 24U;
}

// Every 32-bit numeric field in an ISO 9660 directory record (and the PVD)
// is stored twice: once little-endian, once big-endian, back to back. Both
// copies must stay consistent or strict readers reject the image.
void write_both_endian_u32(std::fstream &stream, std::uint64_t le_offset,
                           std::uint32_t value) {
  std::uint8_t le[4] = {
      static_cast<std::uint8_t>(value & 0xFFU),
      static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
      static_cast<std::uint8_t>((value >> 16U) & 0xFFU),
      static_cast<std::uint8_t>((value >> 24U) & 0xFFU)};
  std::uint8_t be[4] = {le[3], le[2], le[1], le[0]};
  stream.seekp(static_cast<std::streamoff>(le_offset));
  stream.write(reinterpret_cast<const char *>(le), 4);
  stream.write(reinterpret_cast<const char *>(be), 4);
  if (!stream) {
    throw std::runtime_error("failed to patch ISO 9660 directory record");
  }
}

struct DirectoryRecordInfo {
  std::uint64_t record_offset{}; // absolute byte offset of the record itself
  std::uint32_t extent{};
  std::uint32_t size{};
};

std::vector<std::uint8_t> read_range(std::ifstream &stream,
                                     std::uint64_t offset, std::size_t size) {
  stream.clear();
  stream.seekg(static_cast<std::streamoff>(offset));
  std::vector<std::uint8_t> result(size);
  if (size != 0U) {
    stream.read(reinterpret_cast<char *>(result.data()),
                static_cast<std::streamsize>(size));
  }
  if (!stream) {
    throw std::runtime_error("failed while reading ISO image");
  }
  return result;
}

std::string record_name(const std::uint8_t *record, std::size_t length) {
  if (length < 34U) {
    throw std::runtime_error("invalid ISO 9660 directory record");
  }
  const auto name_length = record[32];
  if (33U + name_length > length) {
    throw std::runtime_error("truncated ISO 9660 file name");
  }
  if (name_length == 1U && (record[33] == 0U || record[33] == 1U)) {
    return {};
  }
  std::string name(reinterpret_cast<const char *>(record + 33U), name_length);
  if (const auto version = name.find(';'); version != std::string::npos) {
    name.resize(version);
  }
  while (!name.empty() && name.back() == '.') {
    name.pop_back();
  }
  return name;
}

// Walks the ISO 9660 tree rooted at the primary volume descriptor and
// records, for every file, where its directory record physically lives in
// the image (so it can be patched later) alongside its current extent/size.
std::map<std::string, DirectoryRecordInfo>
index_directory_records(std::ifstream &stream) {
  std::map<std::string, DirectoryRecordInfo> result;

  const auto descriptor = read_range(stream, 16ULL * sector_size, sector_size);
  if (descriptor[0] != 1U ||
      std::string_view(reinterpret_cast<const char *>(descriptor.data() + 1U),
                       5U) != "CD001" ||
      descriptor[6] != 1U) {
    throw std::runtime_error("input is not an ISO 9660 image");
  }
  const auto root_length = descriptor[156];
  if (root_length < 34U || 156U + root_length > descriptor.size()) {
    throw std::runtime_error("ISO image has an invalid root directory");
  }
  const auto root_extent = read_u32(descriptor, 158U);
  const auto root_size = read_u32(descriptor, 166U);

  struct PendingDirectory {
    std::string path;
    std::uint32_t extent;
    std::uint32_t size;
  };
  std::vector<PendingDirectory> pending{{{}, root_extent, root_size}};
  std::set<std::pair<std::uint32_t, std::uint32_t>> visited;
  while (!pending.empty()) {
    const auto directory = pending.back();
    pending.pop_back();
    if (!visited.emplace(directory.extent, directory.size).second) {
      continue;
    }
    const auto base_offset =
        static_cast<std::uint64_t>(directory.extent) * sector_size;
    const auto bytes = read_range(stream, base_offset, directory.size);
    std::size_t cursor = 0;
    while (cursor < bytes.size()) {
      const auto length = bytes[cursor];
      if (length == 0U) {
        cursor = ((cursor / sector_size) + 1U) * sector_size;
        continue;
      }
      if (cursor + length > bytes.size() || length < 34U) {
        throw std::runtime_error("invalid ISO directory record");
      }
      const auto *record = bytes.data() + cursor;
      const auto name = record_name(record, length);
      if (!name.empty()) {
        const auto extent = read_u32(bytes, cursor + 2U);
        const auto size = read_u32(bytes, cursor + 10U);
        const bool is_directory = (record[25] & 2U) != 0U;
        auto entry_path = directory.path.empty() ? name
                                                  : directory.path + "/" + name;
        result[normalized_path(entry_path)] =
            DirectoryRecordInfo{base_offset + cursor, extent, size};
        if (is_directory) {
          pending.push_back({entry_path, extent, size});
        }
      }
      cursor += length;
    }
  }
  return result;
}

void patch_iso_preserving_layout(
    const std::filesystem::path &original, const std::filesystem::path &output,
    const std::vector<Replacement> &replacements) {
  {
    std::ifstream probe(original, std::ios::binary);
    if (!probe) {
      throw std::runtime_error("cannot open ISO image: " + original.string());
    }
  }

  std::map<std::string, DirectoryRecordInfo> records;
  {
    std::ifstream source(original, std::ios::binary);
    records = index_directory_records(source);
  }

  std::filesystem::create_directories(output.parent_path());
  std::filesystem::copy_file(
      original, output, std::filesystem::copy_options::overwrite_existing);

  std::fstream target(output,
                      std::ios::binary | std::ios::in | std::ios::out);
  if (!target) {
    throw std::runtime_error("cannot open ISO image for patching: " +
                             output.string());
  }
  target.seekg(0, std::ios::end);
  const auto original_size = static_cast<std::uint64_t>(target.tellg());
  if (original_size % sector_size != 0U) {
    throw std::runtime_error("ISO image size is not sector aligned");
  }
  std::uint64_t append_sector = original_size / sector_size;
  const auto original_sector_count = append_sector;

  for (const auto &replacement : replacements) {
    const auto key = normalized_path(replacement.iso_path);
    const auto found = records.find(key);
    if (found == records.end()) {
      throw std::runtime_error("ISO image has no file at: " +
                               replacement.iso_path);
    }
    const auto &info = found->second;

    std::ifstream local(replacement.local_path,
                        std::ios::binary | std::ios::ate);
    if (!local) {
      throw std::runtime_error("cannot open replacement file: " +
                               replacement.local_path.string());
    }
    const auto local_size = static_cast<std::uint64_t>(local.tellg());
    local.seekg(0);
    std::vector<char> content(local_size);
    if (local_size != 0U) {
      local.read(content.data(), static_cast<std::streamsize>(local_size));
    }
    if (!local) {
      throw std::runtime_error("failed reading replacement file: " +
                               replacement.local_path.string());
    }

    const std::uint64_t needed_sectors =
        (local_size + sector_size - 1U) / sector_size;
    const std::uint64_t original_sectors =
        (static_cast<std::uint64_t>(info.size) + sector_size - 1U) /
        sector_size;

    std::uint64_t data_sector;
    if (original_sectors != 0U && needed_sectors <= original_sectors) {
      // Fits in the space the original file already occupied: keep the
      // same LBA so every other file's raw-sector references stay valid.
      data_sector = info.extent;
    } else {
      // Does not fit: append past the current end of the image (sector
      // aligned) and repoint only this file's directory record there.
      data_sector = append_sector;
      append_sector += needed_sectors;
    }

    const std::uint64_t write_offset = data_sector * sector_size;
    const std::uint64_t padded_size =
        (data_sector == info.extent ? original_sectors : needed_sectors) *
        sector_size;
    target.seekp(static_cast<std::streamoff>(write_offset));
    target.write(content.data(), static_cast<std::streamsize>(local_size));
    for (std::uint64_t i = local_size; i < padded_size; ++i) {
      target.put('\0');
    }
    if (!target) {
      throw std::runtime_error("failed writing replacement content for: " +
                               replacement.iso_path);
    }

    write_both_endian_u32(target, info.record_offset + 2U,
                          static_cast<std::uint32_t>(data_sector));
    write_both_endian_u32(target, info.record_offset + 10U,
                          static_cast<std::uint32_t>(local_size));
  }

  if (append_sector > original_sector_count) {
    // Keep the PVD's declared volume size consistent with the (now larger)
    // image; several readers sanity-check this against the actual size.
    write_both_endian_u32(target, 16ULL * sector_size + 80U,
                          static_cast<std::uint32_t>(append_sector));
  }

  target.flush();
  if (!target) {
    throw std::runtime_error("failed finalizing patched ISO image: " +
                             output.string());
  }
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc < 3) {
      std::cerr << "usage: " << argv[0]
                << " <original.iso> <output.iso> [<iso_path>=<local_file> "
                   "...]\n";
      return 1;
    }
    const std::filesystem::path original = argv[1];
    const std::filesystem::path output = argv[2];
    std::vector<Replacement> replacements;
    for (int i = 3; i < argc; ++i) {
      const std::string_view argument(argv[i]);
      const auto separator = argument.find('=');
      if (separator == std::string_view::npos) {
        std::cerr << "expected <iso_path>=<local_file>, got: " << argument
                  << '\n';
        return 1;
      }
      replacements.push_back(
          {std::string(argument.substr(0, separator)),
           std::filesystem::path(argument.substr(separator + 1U))});
    }
    patch_iso_preserving_layout(original, output, replacements);
    std::cout << "Built PSP ISO: " << output.string() << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "iso_patch: " << error.what() << '\n';
    return 1;
  }
}
)CPP";
}

std::string macos_cmake(const ExportConfig& config) {
  std::ostringstream out;
  out << "cmake_minimum_required(VERSION 3.20)\n"
         "project("
      << config.project_name
      << " LANGUAGES CXX)\n\n"
         "set(CMAKE_CXX_STANDARD 20)\n"
         "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
         "set(CMAKE_CXX_EXTENSIONS OFF)\n"
         "include(CheckIPOSupported)\n"
         "check_ipo_supported(RESULT PSPRECOMP_IPO_SUPPORTED)\n"
         "include(src/generated/generated_sources.cmake)\n\n"
         "add_subdirectory(refract)\n\n"
         "add_executable("
      << config.project_name
      << " MACOSX_BUNDLE\n"
         "  ${PSPRECOMP_GENERATED_SOURCES}\n"
         "  platform/macos/main.cpp\n"
         "  platform/macos/platform.cpp\n"
         "  src/generated/guest_image.bin\n"
         "  src/generated/relocations.bin\n"
         ")\n"
         "target_include_directories("
      << config.project_name
      << " PRIVATE . include src/generated)\n"
         "target_link_libraries("
      << config.project_name
      << " PRIVATE refract)\n"
         "target_compile_options("
      << config.project_name
      << " PRIVATE -Wno-tautological-compare)\n"
         "if(PSPRECOMP_IPO_SUPPORTED)\n"
         "  set_property(TARGET refract PROPERTY "
         "INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)\n"
         "  set_property(TARGET "
      << config.project_name
      << " PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)\n"
         "endif()\n"
         "set_source_files_properties(\n"
         "  src/generated/guest_image.bin src/generated/relocations.bin\n"
         "  PROPERTIES MACOSX_PACKAGE_LOCATION Resources\n"
         ")\n"
         "set_target_properties("
      << config.project_name
      << " PROPERTIES\n"
         "  MACOSX_BUNDLE_BUNDLE_NAME "
      << toml_string(config.display_name)
      << "\n"
         "  MACOSX_BUNDLE_GUI_IDENTIFIER \"dev.psprecomp."
      << config.project_name
      << "\"\n"
         ")\n";
  return out.str();
}

std::filesystem::path
unique_staging_path(const std::filesystem::path& destination) {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  return destination.parent_path() / ("." + destination.filename().string() +
                                      ".psprecomp-" + std::to_string(nonce));
}

} // namespace

SourceInfo inspect_source(const std::filesystem::path& input) {
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
  result.sfo_path = metadata.sfo_path;
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

ExportSummary export_codebase(const ExportConfig& config) {
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
  if (!std::filesystem::is_directory(config.refract_directory)) {
    throw std::runtime_error("cannot find refract engine at: " +
                             config.refract_directory.string());
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
        for (const auto& entry : iso.entries()) {
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
        std::filesystem::create_directories(staging / "original");
        std::filesystem::copy_file(config.input,
                                   staging / "original" / "disc.iso");
        std::ofstream sectors(staging / "original" / "disc-sectors.tsv",
                              std::ios::binary | std::ios::trunc);
        if (!sectors) {
          throw std::runtime_error("cannot create disc sector metadata");
        }
        for (const auto& entry : iso.entries()) {
          if (!entry.directory) {
            sectors << entry.extent << '\t' << entry.path.generic_string()
                    << '\n';
          }
        }
        if (!sectors) {
          throw std::runtime_error("cannot write disc sector metadata");
        }
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
    } catch (const std::exception& error) {
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
    std::filesystem::copy(config.refract_directory, staging / "refract",
                          std::filesystem::copy_options::recursive);
    GeneratedProjectOptions emitter_options;
    emitter_options.display_name = config.display_name;
    // PspModuleInfo::modname is char[27]. In C++ translation units
    // PSP_MODULE_INFO stringifies its already-quoted name argument, so the
    // literal gains two escaped quote characters on top of the NUL
    // terminator (len + 2 quotes + 1 NUL <= 27 => len <= 24). Names longer
    // than this silently failed to build ("initializer-string ... too
    // long") for any game whose project name exceeded ~24-27 characters.
    emitter_options.module_name = config.project_name.substr(0, 24U);
    emitter_options.target_name = config.project_name;
    emitter_options.include_path = "../../include";
    emitter_options.platform_directory = staging / "platform";
    if (config.progress) {
      config.progress("Translating Allegrex code to C++");
    }
    emit_project(elf, staging / "src" / "generated", map ? &*map : nullptr,
                 emitter_options);

    if (config.progress) {
      config.progress("Writing project files");
    }
    const bool has_disc = info.kind == InputKind::iso && config.extract_disc;
    const auto psp_recompile_mode =
        map && !map->overlay_starts.empty() ? std::string_view("overlays")
                                            : std::string_view("full");
    write_text(staging / "Makefile",
               root_makefile(config, has_disc, info.executable_path,
                             info.sfo_path, psp_recompile_mode));
    write_text(staging / "CMakeLists.txt", macos_cmake(config));
    if (has_disc) {
      write_text(staging / "tools" / "iso_patch.cpp", iso_patch_tool_source());
    }
    write_text(staging / ".gitignore",
               "src/generated/*.o\nsrc/generated/*.elf\n"
               "src/generated/*.prx\nsrc/generated/*.PBP\n"
               "src/generated/PARAM.SFO\nsrc/generated/SND0.AT3\n"
               "disc/\noriginal/\ndist/\nbuild/\n.psprecomp/\n"
               ".refract/\n.DS_Store\n");
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
             << "\npsp_recompile_mode = " << toml_string(psp_recompile_mode)
             << "\ndisc_extracted = " << (has_disc ? "true" : "false") << "\n";
    write_text(staging / "project.toml", manifest.str());

    std::size_t translation_units = 0;
    for (const auto& entry :
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
