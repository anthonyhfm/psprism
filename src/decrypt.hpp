#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace psprecomp {

struct DecryptedExecutable {
  std::vector<std::uint8_t> bytes;
  std::string backend;
};

[[nodiscard]] bool is_elf_data(const std::vector<std::uint8_t> &data) noexcept;
[[nodiscard]] bool
is_encrypted_psp_data(const std::vector<std::uint8_t> &data) noexcept;

// Decrypts a standard ~PSP executable with a compatible local PPSSPP build.
// PSPRecomp intentionally loads the implementation at runtime instead of
// copying PPSSPP's GPL-licensed crypto sources into this repository.
[[nodiscard]] DecryptedExecutable
decrypt_psp_executable(const std::vector<std::uint8_t> &encrypted);

} // namespace psprecomp
