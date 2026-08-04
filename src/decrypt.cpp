#include "decrypt.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace psprecomp {
namespace {

std::vector<std::filesystem::path> ppsspp_candidates() {
  std::vector<std::filesystem::path> result;
  if (const auto *configured = std::getenv("PSPRECOMP_PPSSPP");
      configured != nullptr && *configured != '\0') {
    result.emplace_back(configured);
  }

  if (const auto *path = std::getenv("PATH"); path != nullptr) {
#if defined(_WIN32)
    constexpr char separator = ';';
    constexpr std::string_view names[]{"PPSSPPWindows64.exe",
                                       "PPSSPPWindows.exe"};
#else
    constexpr char separator = ':';
    constexpr std::string_view names[]{"ppsspp", "PPSSPPSDL"};
#endif
    std::string_view remaining(path);
    while (true) {
      const auto end = remaining.find(separator);
      const auto directory = remaining.substr(0, end);
      if (!directory.empty()) {
        for (const auto name : names) {
          result.emplace_back(std::filesystem::path(directory) / name);
        }
      }
      if (end == std::string_view::npos) {
        break;
      }
      remaining.remove_prefix(end + 1U);
    }
  }
#if defined(__APPLE__)
  result.emplace_back(
      "/opt/homebrew/opt/ppsspp/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL");
  result.emplace_back(
      "/usr/local/opt/ppsspp/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL");
  result.emplace_back("/Applications/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL");
  result.emplace_back("/Applications/PPSSPP.app/Contents/MacOS/PPSSPP");
#endif
  return result;
}

#if defined(_WIN32)
using LibraryHandle = HMODULE;

LibraryHandle open_library(const std::filesystem::path &path) {
  return LoadLibraryW(path.c_str());
}

void *find_symbol(LibraryHandle library, const char *name) {
  return reinterpret_cast<void *>(GetProcAddress(library, name));
}

void close_library(LibraryHandle library) { FreeLibrary(library); }
#else
using LibraryHandle = void *;

LibraryHandle open_library(const std::filesystem::path &path) {
  return dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
}

void *find_symbol(LibraryHandle library, const char *name) {
  return dlsym(library, name);
}

void close_library(LibraryHandle library) { dlclose(library); }
#endif

} // namespace

bool is_elf_data(const std::vector<std::uint8_t> &data) noexcept {
  return data.size() >= 4U && data[0] == 0x7fU && data[1] == 'E' &&
         data[2] == 'L' && data[3] == 'F';
}

bool is_encrypted_psp_data(const std::vector<std::uint8_t> &data) noexcept {
  return data.size() >= 4U && data[0] == '~' && data[1] == 'P' &&
         data[2] == 'S' && data[3] == 'P';
}

DecryptedExecutable
decrypt_psp_executable(const std::vector<std::uint8_t> &encrypted) {
  if (!is_encrypted_psp_data(encrypted)) {
    throw std::runtime_error("input is not a ~PSP encrypted executable");
  }
  if (encrypted.size() > 0xffffffffULL) {
    throw std::runtime_error("encrypted PSP executable is too large");
  }

  using DecryptFunction = int (*)(const std::uint8_t *, std::uint8_t *,
                                  std::uint32_t, const std::uint8_t *);
  std::set<std::filesystem::path> attempted;
  for (auto candidate : ppsspp_candidates()) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(candidate, error)) {
      continue;
    }
    candidate = std::filesystem::weakly_canonical(candidate, error);
    if (error || !attempted.insert(candidate).second) {
      continue;
    }
    const auto library = open_library(candidate);
    if (library == nullptr) {
      continue;
    }
    auto *symbol = find_symbol(library, "_Z13pspDecryptPRXPKhPhjS0_");
    if (symbol == nullptr) {
      symbol = find_symbol(library, "pspDecryptPRX");
    }
    if (symbol == nullptr) {
      close_library(library);
      continue;
    }

    const auto decrypt = reinterpret_cast<DecryptFunction>(symbol);
    std::vector<std::uint8_t> output(encrypted.size());
    const auto size =
        decrypt(encrypted.data(), output.data(),
                static_cast<std::uint32_t>(encrypted.size()), nullptr);
    close_library(library);
    if (size <= 0 || static_cast<std::size_t>(size) > output.size()) {
      continue;
    }
    output.resize(static_cast<std::size_t>(size));
    if (!is_elf_data(output)) {
      std::ostringstream message;
      message << "PPSSPP decrypted the executable, but the result is not "
                 "an ELF (magic";
      for (std::size_t i = 0; i < std::min<std::size_t>(4U, output.size());
           ++i) {
        message << ' ' << std::hex << static_cast<unsigned>(output[i]);
      }
      message << "). Compressed PRX payloads are not supported yet.";
      throw std::runtime_error(message.str());
    }
    return {std::move(output), "PPSSPP"};
  }

  throw std::runtime_error(
      "the ISO contains an encrypted ~PSP executable, but no compatible "
      "PPSSPP installation was found. Install PPSSPP or point "
      "PSPRECOMP_PPSSPP at its executable");
}

} // namespace psprecomp
