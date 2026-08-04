#pragma once

#include "elf.hpp"

#include <filesystem>
#include <iosfwd>

namespace psprecomp {

struct GeneratedProjectOptions {
  std::string display_name{"PSP Recompiled"};
  std::string module_name{"psp_recompiled"};
  std::string target_name{"psp_recompiled"};
  std::string include_path{"../../include"};
  std::filesystem::path platform_directory;
  std::uint32_t shard_size{0x4000U};
};

void emit_cpp(const ElfImage &image, const std::filesystem::path &output,
              const CodeMap *code_map = nullptr);
void emit_project(const ElfImage &image, const std::filesystem::path &directory,
                  const CodeMap *code_map = nullptr,
                  const GeneratedProjectOptions &options = {});
bool analyze_coverage(const ElfImage &image, const CodeMap *code_map,
                      std::ostream &output);

} // namespace psprecomp
