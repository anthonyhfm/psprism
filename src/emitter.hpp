#pragma once

#include "elf.hpp"

#include <filesystem>
#include <iosfwd>

namespace psprecomp {

void emit_cpp(const ElfImage& image, const std::filesystem::path& output,
              const CodeMap* code_map = nullptr);
void emit_project(const ElfImage& image, const std::filesystem::path& directory,
                  const CodeMap* code_map = nullptr,
                  std::uint32_t shard_size = 0x4000U);
bool analyze_coverage(const ElfImage& image, const CodeMap* code_map,
                      std::ostream& output);

} // namespace psprecomp
