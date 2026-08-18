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

#include "../emitter.hpp"

namespace psprecomp {
using namespace detail;

bool analyze_coverage(const ElfImage& image, const CodeMap* code_map,
                      std::ostream& output) {
    struct MissingGroup {
        std::size_t count{};
        std::uint32_t example_pc{};
        std::uint32_t example_instruction{};
    };
    std::map<std::string, MissingGroup> invalid;
    std::map<std::string, MissingGroup> guarded;
    std::map<std::string, MissingGroup> fallback;
    std::size_t translated = 0;
    std::size_t excluded = 0;
    for (const auto& section : image.executable_sections) {
        for (std::size_t offset = 0; offset < section.bytes.size();
             offset += 4) {
            const auto pc =
                section.address + static_cast<std::uint32_t>(offset);
            const auto instruction = word_at(section, offset);
            if (code_map != nullptr && !code_map->contains(pc)) {
                ++excluded;
                continue;
            }
            const auto decoded = decode_allegrex(instruction);
            if (decoded.lowering == InstructionLowering::native) {
                ++translated;
            } else if (decoded.lowering ==
                       InstructionLowering::guarded_native) {
                auto& group = guarded[opcode_group(instruction)];
                if (group.count == 0) {
                    group.example_pc = pc;
                    group.example_instruction = instruction;
                }
                ++group.count;
            } else {
                auto& groups =
                    decoded.lowering == InstructionLowering::runtime_fallback
                        ? fallback
                        : invalid;
                auto& group = groups[opcode_group(instruction)];
                if (group.count == 0) {
                    group.example_pc = pc;
                    group.example_instruction = instruction;
                }
                ++group.count;
            }
        }
    }
    output << "translated_words=" << translated << '\n'
           << "excluded_words=" << excluded << '\n';
    std::size_t guarded_total = 0;
    for (const auto& [name, group] : guarded) {
        guarded_total += group.count;
        output << "guarded_native " << name << " count=" << group.count
               << " example_pc=" << hex(group.example_pc)
               << " instruction=" << hex(group.example_instruction) << '\n';
    }
    output << "guarded_native_words=" << guarded_total << '\n';
    std::size_t fallback_total = 0;
    for (const auto& [name, group] : fallback) {
        fallback_total += group.count;
        output << "fallback " << name << " count=" << group.count
               << " example_pc=" << hex(group.example_pc)
               << " instruction=" << hex(group.example_instruction) << '\n';
    }
    output << "fallback_words=" << fallback_total << '\n';
    std::size_t invalid_total = 0;
    for (const auto& [name, group] : invalid) {
        invalid_total += group.count;
        output << "invalid " << name << " count=" << group.count
               << " example_pc=" << hex(group.example_pc)
               << " instruction=" << hex(group.example_instruction) << '\n';
    }
    output << "invalid_words=" << invalid_total << '\n'
           << "unsupported_words=" << invalid_total << '\n';
    return invalid_total == 0;
}

} // namespace psprecomp
