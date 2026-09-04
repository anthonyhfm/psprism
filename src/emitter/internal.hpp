#pragma once

#include "../elf.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace psprecomp::detail {

struct EmittedInstruction {
    std::uint32_t pc{};
    std::uint32_t instruction{};
    std::string code;
    std::optional<std::uint32_t> direct_target;
};

std::vector<std::uint8_t>
compact_relocations(const std::vector<Relocation>& relocations);
std::string import_symbol(const Import& import, const CodeMap* code_map);
std::uint32_t word_at(const ExecutableSection& section, std::size_t offset);
std::string hex(std::uint32_t value);
std::string hex_address(std::uint32_t value);
std::string pc_label(std::uint32_t value);
std::string function_name(std::uint32_t value);
std::string cpp_string(std::string_view value);
std::string make_value(std::string_view value);
std::string identifier(std::string_view value);
bool starts_delayed_branch(std::uint32_t instruction);
bool needs_post_delay_entry(std::uint32_t instruction);
bool may_stop_execution(std::uint32_t instruction);
bool is_executable_word(const ElfImage& image, std::uint32_t address);
std::set<std::uint32_t> relocated_words(const ElfImage& image);
std::map<std::uint32_t, std::uint32_t>
jump_relocation_bases(const ElfImage& image);
std::optional<std::uint32_t> direct_control_target(
    std::uint32_t pc, std::uint32_t instruction,
    const std::map<std::uint32_t, std::uint32_t>& jump_bases,
    std::uint32_t preferred_base = 0);
std::set<std::uint32_t> potential_block_entries(const ElfImage& image,
                                                const CodeMap* code_map,
                                                std::uint32_t entry,
                                                std::uint32_t shard_size);
struct DiscoveredFunction {
    std::uint32_t start{};
    std::string symbol;
    std::string filename;
};

std::vector<DiscoveredFunction> discover_functions(
    const ElfImage& image, const CodeMap* code_map);
std::string emit_instruction(std::uint32_t pc, std::uint32_t instruction,
                             bool relocated = true,
                             std::uint32_t preferred_base = 0);
std::string opcode_group(std::uint32_t instruction);

} // namespace psprecomp::detail
