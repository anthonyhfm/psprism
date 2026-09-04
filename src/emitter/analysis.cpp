#include "../emitter.hpp"
#include "internal.hpp"
#include <psprecomp/allegrex_decoder.hpp>
#include <psprecomp/vfpu.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace psprecomp::detail
{

    std::set<std::uint32_t> relocated_words(const ElfImage &image)
    {
        std::set<std::uint32_t> result;
        for (const auto &relocation : image.relocations)
        {
            const auto segment = std::find_if(
                image.load_segments.begin(), image.load_segments.end(),
                [&](const LoadSegment &candidate)
                {
                    return candidate.program_index == relocation.patch_segment;
                });
            if (segment != image.load_segments.end() &&
                relocation.offset <= segment->memory_size &&
                segment->memory_size - relocation.offset >= 4U)
            {
                result.insert(segment->address + relocation.offset);
            }
        }
        return result;
    }

    const LoadSegment *find_load_segment(const ElfImage &image,
                                         std::uint8_t program_index)
    {
        const auto segment =
            std::find_if(image.load_segments.begin(), image.load_segments.end(),
                         [&](const LoadSegment &candidate)
                         {
                             return candidate.program_index == program_index;
                         });
        return segment == image.load_segments.end() ? nullptr : &*segment;
    }

    std::map<std::uint32_t, std::uint32_t>
    jump_relocation_bases(const ElfImage &image)
    {
        std::map<std::uint32_t, std::uint32_t> result;
        for (const auto &relocation : image.relocations)
        {
            if (relocation.type != 4U)
            {
                continue;
            }
            const auto *patch = find_load_segment(image, relocation.patch_segment);
            const auto *target =
                find_load_segment(image, relocation.target_segment);
            if (patch != nullptr && target != nullptr)
            {
                result[patch->address + relocation.offset] = target->address;
            }
        }
        return result;
    }

    std::optional<std::uint32_t> direct_control_target(
        std::uint32_t pc, std::uint32_t instruction,
        const std::map<std::uint32_t, std::uint32_t> &jump_bases,
        std::uint32_t preferred_base)
    {
        if (!starts_delayed_branch(instruction))
        {
            return std::nullopt;
        }
        const auto op = instruction >> 26U;
        if (op == 0U)
        {
            return std::nullopt; // JR/JALR are genuinely dynamic.
        }
        if (op == 0x02U || op == 0x03U)
        {
            auto target_field = instruction & 0x03ffffffU;
            if (const auto relocation = jump_bases.find(pc);
                relocation != jump_bases.end())
            {
                target_field =
                    (target_field + (relocation->second >> 2U)) & 0x03ffffffU;
            }
            const auto original_pc = pc + preferred_base;
            const auto absolute_target =
                ((original_pc + 4U) & 0xf0000000U) | (target_field << 2U);
            return absolute_target - preferred_base;
        }
        const auto displacement = static_cast<std::uint32_t>(
            static_cast<std::int32_t>(static_cast<std::int16_t>(instruction)) * 4);
        return pc + 4U + displacement;
    }

    bool is_executable_word(const ElfImage &image, std::uint32_t address)
    {
        if ((address & 3U) != 0U)
        {
            return false;
        }
        return std::any_of(
            image.executable_sections.begin(), image.executable_sections.end(),
            [&](const ExecutableSection &section)
            {
                return address >= section.address &&
                       static_cast<std::uint64_t>(address) + 4U <=
                           static_cast<std::uint64_t>(section.address) +
                               section.bytes.size();
            });
    }

    std::set<std::uint32_t> potential_block_entries(const ElfImage &image,
                                                    const CodeMap *code_map,
                                                    std::uint32_t entry,
                                                    std::uint32_t shard_size)
    {
        std::set<std::uint32_t> result;
        const auto add = [&](std::uint32_t address)
        {
            if (is_executable_word(image, address))
            {
                result.insert(address);
            }
        };
        add(entry);
        for (const auto &section : image.executable_sections)
        {
            add(section.address);
            const auto section_end =
                static_cast<std::uint64_t>(section.address) + section.bytes.size();
            auto boundary =
                (section.address + shard_size - 1U) & ~(shard_size - 1U);
            while (boundary < section_end)
            {
                add(boundary);
                boundary += shard_size;
            }
        }
        if (code_map != nullptr)
        {
            for (const auto address : code_map->function_starts)
            {
                add(address);
            }
            for (const auto address : code_map->block_entries)
            {
                add(address);
            }
        }

        const auto jump_bases = jump_relocation_bases(image);

        // Every statically visible control-flow destination is a dispatcher entry.
        // Returns from JAL/JALR and the fall-through side of branches land at pc+8.
        for (const auto &section : image.executable_sections)
        {
            for (std::size_t offset = 0; offset < section.bytes.size();
                 offset += 4)
            {
                const auto pc =
                    section.address + static_cast<std::uint32_t>(offset);
                const auto instruction = word_at(section, offset);
                if (!starts_delayed_branch(instruction))
                {
                    continue;
                }
                if (needs_post_delay_entry(instruction))
                {
                    add(pc + 8U);
                }
                if (const auto target =
                        direct_control_target(pc, instruction, jump_bases,
                                              image.preferred_base))
                {
                    add(*target);
                }
            }
        }

        // Function pointers, virtual tables and callback tables in a PRX carry
        // R_MIPS_32 relocations.  Raw pointer scanning is only appropriate for a
        // fixed-address ELF: PRX data contains many small numeric values which
        // would otherwise look like accidental code pointers.
        const auto memory = image.memory_image();
        if (image.relocations.empty())
        {
            for (std::size_t offset = 0; offset + 4U <= memory.size();
                 offset += 4U)
            {
                const auto value =
                    static_cast<std::uint32_t>(memory[offset]) |
                    static_cast<std::uint32_t>(memory[offset + 1U]) << 8U |
                    static_cast<std::uint32_t>(memory[offset + 2U]) << 16U |
                    static_cast<std::uint32_t>(memory[offset + 3U]) << 24U;
                if (image.preferred_base != 0 && value >= image.preferred_base)
                {
                    add(value - image.preferred_base);
                }
                else
                {
                    add(value);
                }
            }
        }
        // Addresses passed directly in registers (thread entries, callbacks,
        // constructors, and similar) are commonly materialized by a relocated
        // LUI/ADDIU pair rather than stored as an R_MIPS_32 data pointer.
        for (std::size_t i = 0; i < image.relocations.size(); ++i)
        {
            const auto &high = image.relocations[i];
            if (high.type != 5U)
            {
                continue;
            }
            const Relocation *low = nullptr;
            for (std::size_t j = i + 1U; j < image.relocations.size(); ++j)
            {
                if (image.relocations[j].type == 6U &&
                    image.relocations[j].target_segment == high.target_segment)
                {
                    low = &image.relocations[j];
                    break;
                }
            }
            const auto *high_patch = find_load_segment(image, high.patch_segment);
            const auto *low_patch =
                low != nullptr ? find_load_segment(image, low->patch_segment)
                               : nullptr;
            const auto *target = find_load_segment(image, high.target_segment);
            if (high_patch == nullptr || low_patch == nullptr ||
                target == nullptr)
            {
                continue;
            }
            const auto high_address = high_patch->address + high.offset;
            const auto low_address = low_patch->address + low->offset;
            if (high_address > memory.size() || memory.size() - high_address < 4U ||
                low_address > memory.size() || memory.size() - low_address < 4U)
            {
                continue;
            }
            const auto high_word =
                static_cast<std::uint32_t>(memory[high_address]) |
                static_cast<std::uint32_t>(memory[high_address + 1U]) << 8U |
                static_cast<std::uint32_t>(memory[high_address + 2U]) << 16U |
                static_cast<std::uint32_t>(memory[high_address + 3U]) << 24U;
            const auto low_word =
                static_cast<std::uint32_t>(memory[low_address]) |
                static_cast<std::uint32_t>(memory[low_address + 1U]) << 8U |
                static_cast<std::uint32_t>(memory[low_address + 2U]) << 16U |
                static_cast<std::uint32_t>(memory[low_address + 3U]) << 24U;
            auto value = (high_word & 0xffffU) << 16U;
            value += static_cast<std::uint32_t>(static_cast<std::int32_t>(
                static_cast<std::int16_t>(low_word & 0xffffU)));
            add(value + target->address);
        }
        for (const auto &relocation : image.relocations)
        {
            if (relocation.type != 2U)
            {
                continue;
            }
            const auto *patch = find_load_segment(image, relocation.patch_segment);
            const auto *target =
                find_load_segment(image, relocation.target_segment);
            if (patch == nullptr || target == nullptr)
            {
                continue;
            }
            const auto address = patch->address + relocation.offset;
            if (is_executable_word(image, address) || address > memory.size() ||
                memory.size() - address < 4U)
            {
                continue;
            }
            const auto value =
                static_cast<std::uint32_t>(memory[address]) |
                static_cast<std::uint32_t>(memory[address + 1U]) << 8U |
                static_cast<std::uint32_t>(memory[address + 2U]) << 16U |
                static_cast<std::uint32_t>(memory[address + 3U]) << 24U;
            add(value + target->address);
        }
        return result;
    }

    std::vector<DiscoveredFunction> discover_functions(
        const ElfImage &image, const CodeMap *code_map)
    {
        std::set<std::uint32_t> starts;
        std::set<std::uint32_t> import_stubs;
        for (const auto &import : image.imports)
        {
            import_stubs.insert(import.stub_address);
        }

        const auto add = [&](std::uint32_t address)
        {
            if (is_executable_word(image, address) && !import_stubs.contains(address))
            {
                starts.insert(address);
            }
        };

        if (code_map != nullptr && !code_map->function_starts.empty())
        {
            for (const auto addr : code_map->function_starts)
            {
                add(addr);
            }
        }
        else
        {
            // 1. Entry point
            add(image.entry);
            if (code_map != nullptr)
            {
                if (code_map->entry != 0U)
                {
                    add(code_map->entry);
                }
                for (const auto &range : code_map->function_ranges)
                {
                    add(range.begin);
                }
                for (const auto &sym : code_map->function_symbols)
                {
                    add(sym.address);
                }
            }

            // 2. Section starts
            for (const auto &section : image.executable_sections)
            {
                add(section.address);
            }

            // 3. Scan executable sections for JAL/BAL targets, function prologues, returns
            const auto jump_bases = jump_relocation_bases(image);
            for (const auto &section : image.executable_sections)
            {
                for (std::size_t offset = 0; offset < section.bytes.size(); offset += 4)
                {
                    const auto pc = section.address + static_cast<std::uint32_t>(offset);
                    const auto instruction = word_at(section, offset);
                    const auto op = instruction >> 26U;

                    // JAL
                    if (op == 0x03U)
                    {
                        if (const auto target = direct_control_target(pc, instruction, jump_bases, image.preferred_base))
                        {
                            add(*target);
                        }
                    }
                    else if (op == 0x01U)
                    {
                        const auto rt = (instruction >> 16U) & 31U;
                        if (rt >= 0x10U && rt <= 0x13U)
                        {
                            const auto disp = static_cast<std::uint32_t>(
                                static_cast<std::int32_t>(static_cast<std::int16_t>(instruction & 0xffffU)) * 4);
                            add(pc + 4U + disp);
                        }
                    }

                    // Function prologues: addiu $sp, $sp, -imm
                    if ((instruction & 0xffff8000U) == 0x27bd8000U)
                    {
                        add(pc);
                    }

                    // After jr $ra (0x03e00008) + delay slot: next instruction is likely a new function
                    if (instruction == 0x03e00008U)
                    {
                        const auto next_pc = pc + 8U;
                        if (offset + 8 < section.bytes.size())
                        {
                            add(next_pc);
                        }
                    }
                }
            }

            // Relocated function pointers
            const auto memory = image.memory_image();
            for (const auto &relocation : image.relocations)
            {
                if (relocation.type == 2U)
                {
                    const auto *patch = find_load_segment(image, relocation.patch_segment);
                    const auto *target = find_load_segment(image, relocation.target_segment);
                    if (patch != nullptr && target != nullptr)
                    {
                        const auto address = patch->address + relocation.offset;
                        if (address + 4U <= memory.size())
                        {
                            const auto value =
                                static_cast<std::uint32_t>(memory[address]) |
                                (static_cast<std::uint32_t>(memory[address + 1U]) << 8U) |
                                (static_cast<std::uint32_t>(memory[address + 2U]) << 16U) |
                                (static_cast<std::uint32_t>(memory[address + 3U]) << 24U);
                            add(value + target->address);
                        }
                    }
                }
            }
        }

        std::vector<DiscoveredFunction> result;
        result.reserve(starts.size());
        for (const auto start : starts)
        {
            std::string symbol;
            if (code_map != nullptr)
            {
                if (const auto *s = code_map->symbol_at(start))
                {
                    symbol = *s;
                }
                else if (const auto *r = code_map->function_containing(start))
                {
                    symbol = r->name;
                }
            }
            std::ostringstream filename;
            filename << "func_" << std::hex << std::setfill('0') << std::setw(8) << start;
            if (!symbol.empty())
            {
                filename << "_" << identifier(symbol);
            }
            filename << ".cpp";
            result.push_back({start, symbol, filename.str()});
        }
        return result;
    }

} // namespace psprecomp::detail
