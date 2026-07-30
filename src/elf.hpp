#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace psprecomp {

struct ExecutableSection {
    std::string name;
    std::uint32_t address{};
    std::vector<std::uint8_t> bytes;
};

struct LoadSegment {
    std::uint8_t program_index{};
    std::uint32_t address{};
    std::uint32_t memory_size{};
    std::uint32_t flags{};
    std::vector<std::uint8_t> bytes;
};

struct Relocation {
    std::uint32_t offset{};
    std::uint8_t type{};
    std::uint8_t patch_segment{};
    std::uint8_t target_segment{};
};

struct Import {
    std::string library;
    std::uint32_t nid{};
    std::uint32_t stub_address{};
};

struct ElfImage {
    std::uint32_t entry{};
    std::vector<ExecutableSection> executable_sections;
    std::vector<LoadSegment> load_segments;
    std::vector<Relocation> relocations;
    std::vector<Import> imports;

    [[nodiscard]] std::vector<std::uint8_t> memory_image() const;
    [[nodiscard]] std::uint32_t memory_size() const;
};

struct AddressRange {
    std::uint32_t begin{};
    std::uint32_t end{};
};

struct FunctionSymbol {
    std::uint32_t address{};
    std::string name;
};

struct CodeMap {
    std::uint32_t entry{};
    std::vector<std::uint32_t> function_starts;
    std::vector<FunctionSymbol> function_symbols;
    std::vector<AddressRange> excluded_ranges;

    [[nodiscard]] bool contains(std::uint32_t address) const;
    [[nodiscard]] const std::string* symbol_at(std::uint32_t address) const;
};

ElfImage load_elf(const std::filesystem::path& path);
CodeMap load_code_map(const std::filesystem::path& path);

} // namespace psprecomp
