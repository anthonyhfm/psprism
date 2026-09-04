#include "hash.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      std::cerr << "check failed: " #expression << " at " << __FILE__ << ':'   \
                << __LINE__ << '\n';                                           \
      return 1;                                                                \
    }                                                                          \
  } while (false)

int main() {
  const std::string abc = "abc";
  CHECK(psprecomp::sha256_hex(std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(abc.data()), abc.size())) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

  const auto path = std::filesystem::temp_directory_path() /
                    ("psprism-hash-test-" +
                     std::to_string(std::chrono::steady_clock::now()
                                        .time_since_epoch()
                                        .count()));
  {
    std::ofstream output(path, std::ios::binary);
    output << abc;
  }
  CHECK(psprecomp::sha256_file(path) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  std::filesystem::remove(path);
  return 0;
}
