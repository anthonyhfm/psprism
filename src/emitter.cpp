#include "emitter.hpp"
#include "nids.hpp"

#include <psprecomp/allegrex_decoder.hpp>
#include <psprecomp/vfpu.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace psprecomp {
namespace {

std::vector<std::uint8_t>
compact_relocations(const std::vector<Relocation>& relocations) {
    std::vector<std::uint8_t> result;
    result.reserve(relocations.size() * 2U);
    std::array<std::uint32_t, 256> previous_offsets{};
    Relocation previous;
    bool have_metadata = false;
    for (const auto& relocation : relocations) {
        const auto previous_offset = previous_offsets[relocation.patch_segment];
        const auto delta = static_cast<std::int64_t>(relocation.offset) -
                           static_cast<std::int64_t>(previous_offset);
        std::uint8_t type_code = 0xffU;
        switch (relocation.type) {
        case 0:
            type_code = 0U;
            break;
        case 2:
            type_code = 1U;
            break;
        case 4:
            type_code = 2U;
            break;
        case 5:
            type_code = 3U;
            break;
        case 6:
            type_code = 4U;
            break;
        default:
            break;
        }
        const bool can_use_short =
            type_code != 0xffU && relocation.patch_segment < 2U &&
            relocation.target_segment < 2U && delta >= 0 &&
            (delta & 3) == 0 && delta <= 28;
        if (can_use_short) {
            result.push_back(static_cast<std::uint8_t>(
                (static_cast<std::uint64_t>(delta) / 4U) << 5U |
                (relocation.target_segment << 4U) |
                (relocation.patch_segment << 3U) | type_code));
            previous_offsets[relocation.patch_segment] = relocation.offset;
            previous = relocation;
            have_metadata = true;
            continue;
        }

        std::uint8_t extended_short_code = 0xffU;
        if (relocation.type == 2U) {
            extended_short_code = 5U;
        } else if (relocation.type == 4U) {
            extended_short_code = 6U;
        } else if (relocation.type == 6U) {
            extended_short_code = 7U;
        }
        const bool can_use_extended_short =
            extended_short_code != 0xffU && relocation.patch_segment == 0U &&
            relocation.target_segment == 0U && delta >= 0 &&
            (delta & 3) == 0 && delta <= 124 &&
            !(extended_short_code == 7U && delta == 124);
        if (can_use_extended_short) {
            result.push_back(static_cast<std::uint8_t>(
                (static_cast<std::uint64_t>(delta) / 4U) << 3U |
                extended_short_code));
            previous_offsets[relocation.patch_segment] = relocation.offset;
            previous = relocation;
            have_metadata = true;
            continue;
        }

        result.push_back(0xffU);
        const auto zigzag = delta >= 0
                                ? static_cast<std::uint64_t>(delta) * 2U
                                : static_cast<std::uint64_t>(-(delta + 1)) *
                                          2U +
                                      1U;
        const bool metadata_changed =
            !have_metadata || relocation.type != previous.type ||
            relocation.patch_segment != previous.patch_segment ||
            relocation.target_segment != previous.target_segment;
        auto tag = (zigzag << 1U) |
                   static_cast<std::uint64_t>(metadata_changed);
        do {
            auto byte = static_cast<std::uint8_t>(tag & 0x7fU);
            tag >>= 7U;
            if (tag != 0U) {
                byte |= 0x80U;
            }
            result.push_back(byte);
        } while (tag != 0U);
        if (metadata_changed) {
            const bool can_pack = relocation.type < 16U &&
                                  relocation.patch_segment < 4U &&
                                  relocation.target_segment < 4U;
            const auto packed = static_cast<std::uint8_t>(
                relocation.type | (relocation.patch_segment << 4U) |
                (relocation.target_segment << 6U));
            if (can_pack && packed != 0xffU) {
                result.push_back(packed);
            } else {
                result.push_back(0xffU);
                result.push_back(relocation.type);
                result.push_back(relocation.patch_segment);
                result.push_back(relocation.target_segment);
            }
        }
        previous_offsets[relocation.patch_segment] = relocation.offset;
        previous = relocation;
        have_metadata = true;
    }
    return result;
}

std::string import_symbol(const Import& import, const CodeMap* code_map) {
    if (code_map != nullptr) {
        if (const auto* symbol = code_map->symbol_at(import.stub_address)) {
            return *symbol;
        }
    }
    if (const auto symbol = resolve_psp_nid(import.library, import.nid);
        !symbol.empty()) {
        return std::string(symbol);
    }
    return std::string(resolve_psp_nid(import.nid));
}

std::uint32_t word_at(const ExecutableSection& section, std::size_t offset) {
    const auto* bytes = section.bytes.data() + offset;
    return static_cast<std::uint32_t>(bytes[0]) |
           static_cast<std::uint32_t>(bytes[1]) << 8U |
           static_cast<std::uint32_t>(bytes[2]) << 16U |
           static_cast<std::uint32_t>(bytes[3]) << 24U;
}

std::string hex(std::uint32_t value) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::setfill('0') << std::setw(8) << value
           << 'U';
    return stream.str();
}

std::string hex_address(std::uint32_t value) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::setfill('0') << std::setw(8) << value;
    return stream.str();
}

std::string pc_label(std::uint32_t value) {
    std::ostringstream stream;
    stream << "pc_" << std::hex << std::setfill('0') << std::setw(8) << value;
    return stream.str();
}

std::string function_name(std::uint32_t value) {
    std::ostringstream stream;
    stream << "run_function_" << std::hex << std::setfill('0') << std::setw(8)
           << value;
    return stream.str();
}

std::string cpp_string(std::string_view value) {
    std::string result;
    for (const auto character : value) {
        if (character == '\\' || character == '"') {
            result.push_back('\\');
        }
        if (character != '\r' && character != '\n') {
            result.push_back(character);
        }
    }
    return result;
}

std::string make_value(std::string_view value) {
    std::string result;
    for (const auto character : value) {
        if (character == '$') {
            result += "$$";
        } else if (character == '\'') {
            result += "'\\''";
        } else {
            result.push_back(character == '\r' || character == '\n' ||
                                     character == '#'
                                 ? ' '
                                 : character);
        }
    }
    return result;
}

std::string identifier(std::string_view value) {
    std::string result;
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        result.push_back(std::isalnum(byte) || character == '_' ? character
                                                                : '_');
    }
    if (result.empty() || std::isdigit(static_cast<unsigned char>(result[0]))) {
        result.insert(0, "import_");
    }
    return result;
}

bool starts_delayed_branch(std::uint32_t instruction) {
    return decode_allegrex(instruction).has_flag(instruction_delayed_branch);
}

bool needs_post_delay_entry(std::uint32_t instruction) {
    // Compilers place jump-table landing pads directly after unconditional
    // branch delay slots (Duff-style loops are a common example). The target
    // is reached through JR/JALR, so static direct-target discovery cannot see
    // it. Registering pc + 8 for every delayed control transfer is harmless
    // for normal fall-through and makes these indirect entries dispatchable.
    return starts_delayed_branch(instruction);
}

bool may_stop_execution(std::uint32_t instruction) {
    const auto op = instruction >> 26U;
    if (op == 0) {
        const auto function = instruction & 63U;
        return function == 0x0cU || function == 0x0dU;
    }
    return false;
}

struct EmittedInstruction {
    std::uint32_t pc{};
    std::uint32_t instruction{};
    std::string code;
    std::optional<std::uint32_t> direct_target;
};

std::set<std::uint32_t> relocated_words(const ElfImage& image) {
    std::set<std::uint32_t> result;
    for (const auto& relocation : image.relocations) {
        const auto segment = std::find_if(
            image.load_segments.begin(), image.load_segments.end(),
            [&](const LoadSegment& candidate) {
                return candidate.program_index == relocation.patch_segment;
            });
        if (segment != image.load_segments.end() &&
            relocation.offset <= segment->memory_size &&
            segment->memory_size - relocation.offset >= 4U) {
            result.insert(segment->address + relocation.offset);
        }
    }
    return result;
}

const LoadSegment* find_load_segment(const ElfImage& image,
                                     std::uint8_t program_index) {
    const auto segment =
        std::find_if(image.load_segments.begin(), image.load_segments.end(),
                     [&](const LoadSegment& candidate) {
                         return candidate.program_index == program_index;
                     });
    return segment == image.load_segments.end() ? nullptr : &*segment;
}

std::map<std::uint32_t, std::uint32_t>
jump_relocation_bases(const ElfImage& image) {
    std::map<std::uint32_t, std::uint32_t> result;
    for (const auto& relocation : image.relocations) {
        if (relocation.type != 4U) {
            continue;
        }
        const auto* patch = find_load_segment(image, relocation.patch_segment);
        const auto* target =
            find_load_segment(image, relocation.target_segment);
        if (patch != nullptr && target != nullptr) {
            result[patch->address + relocation.offset] = target->address;
        }
    }
    return result;
}

std::optional<std::uint32_t> direct_control_target(
    std::uint32_t pc, std::uint32_t instruction,
    const std::map<std::uint32_t, std::uint32_t>& jump_bases,
    std::uint32_t preferred_base = 0) {
    if (!starts_delayed_branch(instruction)) {
        return std::nullopt;
    }
    const auto op = instruction >> 26U;
    if (op == 0U) {
        return std::nullopt; // JR/JALR are genuinely dynamic.
    }
    if (op == 0x02U || op == 0x03U) {
        auto target_field = instruction & 0x03ffffffU;
        if (const auto relocation = jump_bases.find(pc);
            relocation != jump_bases.end()) {
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

bool is_executable_word(const ElfImage& image, std::uint32_t address) {
    if ((address & 3U) != 0U) {
        return false;
    }
    return std::any_of(
        image.executable_sections.begin(), image.executable_sections.end(),
        [&](const ExecutableSection& section) {
            return address >= section.address &&
                   static_cast<std::uint64_t>(address) + 4U <=
                       static_cast<std::uint64_t>(section.address) +
                           section.bytes.size();
        });
}

std::set<std::uint32_t> potential_block_entries(const ElfImage& image,
                                                const CodeMap* code_map,
                                                std::uint32_t entry,
                                                std::uint32_t shard_size) {
    std::set<std::uint32_t> result;
    const auto add = [&](std::uint32_t address) {
        if (is_executable_word(image, address)) {
            result.insert(address);
        }
    };
    add(entry);
    for (const auto& section : image.executable_sections) {
        add(section.address);
        const auto section_end =
            static_cast<std::uint64_t>(section.address) + section.bytes.size();
        auto boundary =
            (section.address + shard_size - 1U) & ~(shard_size - 1U);
        while (boundary < section_end) {
            add(boundary);
            boundary += shard_size;
        }
    }
    if (code_map != nullptr) {
        for (const auto address : code_map->function_starts) {
            add(address);
        }
        for (const auto address : code_map->block_entries) {
            add(address);
        }
    }

    const auto jump_bases = jump_relocation_bases(image);

    // Every statically visible control-flow destination is a dispatcher entry.
    // Returns from JAL/JALR and the fall-through side of branches land at pc+8.
    for (const auto& section : image.executable_sections) {
        for (std::size_t offset = 0; offset < section.bytes.size();
             offset += 4) {
            const auto pc =
                section.address + static_cast<std::uint32_t>(offset);
            const auto instruction = word_at(section, offset);
            if (!starts_delayed_branch(instruction)) {
                continue;
            }
            if (needs_post_delay_entry(instruction)) {
                add(pc + 8U);
            }
            if (const auto target =
                    direct_control_target(pc, instruction, jump_bases,
                                          image.preferred_base)) {
                add(*target);
            }
        }
    }

    // Function pointers, virtual tables and callback tables in a PRX carry
    // R_MIPS_32 relocations.  Raw pointer scanning is only appropriate for a
    // fixed-address ELF: PRX data contains many small numeric values which
    // would otherwise look like accidental code pointers.
    const auto memory = image.memory_image();
    if (image.relocations.empty()) {
        for (std::size_t offset = 0; offset + 4U <= memory.size();
             offset += 4U) {
            const auto value =
                static_cast<std::uint32_t>(memory[offset]) |
                static_cast<std::uint32_t>(memory[offset + 1U]) << 8U |
                static_cast<std::uint32_t>(memory[offset + 2U]) << 16U |
                static_cast<std::uint32_t>(memory[offset + 3U]) << 24U;
            if (image.preferred_base != 0 && value >= image.preferred_base) {
                add(value - image.preferred_base);
            } else {
                add(value);
            }
        }
    }
    // Addresses passed directly in registers (thread entries, callbacks,
    // constructors, and similar) are commonly materialized by a relocated
    // LUI/ADDIU pair rather than stored as an R_MIPS_32 data pointer.
    for (std::size_t i = 0; i < image.relocations.size(); ++i) {
        const auto& high = image.relocations[i];
        if (high.type != 5U) {
            continue;
        }
        const Relocation* low = nullptr;
        for (std::size_t j = i + 1U; j < image.relocations.size(); ++j) {
            if (image.relocations[j].type == 6U &&
                image.relocations[j].target_segment == high.target_segment) {
                low = &image.relocations[j];
                break;
            }
        }
        const auto* high_patch = find_load_segment(image, high.patch_segment);
        const auto* low_patch =
            low != nullptr ? find_load_segment(image, low->patch_segment)
                           : nullptr;
        const auto* target = find_load_segment(image, high.target_segment);
        if (high_patch == nullptr || low_patch == nullptr ||
            target == nullptr) {
            continue;
        }
        const auto high_address = high_patch->address + high.offset;
        const auto low_address = low_patch->address + low->offset;
        if (high_address > memory.size() || memory.size() - high_address < 4U ||
            low_address > memory.size() || memory.size() - low_address < 4U) {
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
    for (const auto& relocation : image.relocations) {
        if (relocation.type != 2U) {
            continue;
        }
        const auto* patch = find_load_segment(image, relocation.patch_segment);
        const auto* target =
            find_load_segment(image, relocation.target_segment);
        if (patch == nullptr || target == nullptr) {
            continue;
        }
        const auto address = patch->address + relocation.offset;
        if (is_executable_word(image, address) || address > memory.size() ||
            memory.size() - address < 4U) {
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

std::string reg(std::uint32_t index) {
    return "state.gpr[" + std::to_string(index) + "]";
}

std::string immediate(std::uint32_t instruction, bool relocated) {
    if (!relocated) {
        return hex(instruction & 0xffffU);
    }
    return "instruction_immediate(state, current_pc, " + hex(instruction) + ")";
}

std::string signed_immediate(std::uint32_t instruction, bool relocated) {
    if (!relocated) {
        return hex(static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::int16_t>(instruction & 0xffffU))));
    }
    return "instruction_signed_immediate(state, current_pc, " +
           hex(instruction) + ")";
}

std::string signed_immediate_s32(std::uint32_t instruction, bool relocated) {
    if (!relocated) {
        return "static_cast<std::int32_t>(" +
               signed_immediate(instruction, false) + ")";
    }
    return "instruction_signed_immediate_s32(state, current_pc, " +
           hex(instruction) + ")";
}

std::string signed_imm(std::uint32_t instruction, bool relocated) {
    return " + " + signed_immediate(instruction, relocated);
}

std::string branch_target(std::uint32_t pc, std::uint32_t instruction,
                          bool relocated) {
    if (!relocated) {
        const auto displacement = static_cast<std::uint32_t>(
            static_cast<std::int32_t>(static_cast<std::int16_t>(instruction)) *
            4);
        return "state.memory_base + " + hex(pc + 4U + displacement);
    }
    return "instruction_branch_target(state, current_pc, " + hex(instruction) +
           ")";
}

std::string jump_target(std::uint32_t pc, std::uint32_t instruction,
                        bool relocated, std::uint32_t preferred_base) {
    if (!relocated) {
        const auto original_pc = pc + preferred_base;
        const auto target =
            (((original_pc + 4U) & 0xf0000000U) |
             ((instruction & 0x03ffffffU) << 2U)) -
            preferred_base;
        return "state.memory_base + " + hex(target);
    }
    return "instruction_jump_target(state, current_pc, " + hex(instruction) +
           ")";
}

std::string opcode_group(std::uint32_t instruction) {
    const auto decoded = decode_allegrex(instruction);
    if (decoded.valid()) {
        return std::string(decoded.name);
    }
    const auto op = instruction >> 26U;
    std::ostringstream stream;
    stream << "op_" << std::hex << std::setfill('0') << std::setw(2) << op;
    if (op == 0) {
        stream << "_special_" << std::setw(2) << (instruction & 63U);
    } else if (op == 1) {
        stream << "_regimm_" << std::setw(2) << ((instruction >> 16U) & 31U);
    } else if (op >= 0x10 && op <= 0x12) {
        stream << "_rs_" << std::setw(2) << ((instruction >> 21U) & 31U);
    }
    return stream.str();
}

std::string emit_instruction(std::uint32_t pc, std::uint32_t instruction,
                             bool relocated = true,
                             std::uint32_t preferred_base = 0) {
    const auto decoded = decode_allegrex(instruction);
    const auto op = decoded.op;
    const auto rs = decoded.rs;
    const auto rt = decoded.rt;
    const auto rd = decoded.rd;
    const auto shift = decoded.shift;
    const auto function = decoded.function;
    std::ostringstream out;
    if (!decoded.valid()) {
        return "/* unsupported/reserved PSP word */ "
               "state.stop_reason = StopReason::invalid_pc;";
    }
    if (decoded.lowering == InstructionLowering::runtime_fallback) {
        out << "execute_vfpu(state, " << hex(instruction)
            << ", state.memory_base + " << hex(pc) << ");";
        return out.str();
    }
    if (op == 0) {
        switch (function) {
        case 0x00:
            if (instruction == 0) {
                out << "/* nop */";
            } else {
                out << reg(rd) << " = " << reg(rt) << " << " << shift << "U;";
            }
            break;
        case 0x02:
            if (rs == 1) {
                out << reg(rd) << " = rotate_right(" << reg(rt) << ", " << shift
                    << "U);";
            } else if (rs == 0) {
                out << reg(rd) << " = " << reg(rt) << " >> " << shift << "U;";
            } else {
                return "/* unsupported/reserved PSP word */ "
                       "state.stop_reason = StopReason::invalid_pc;";
            }
            break;
        case 0x03:
            out << reg(rd) << " = arithmetic_shift_right(" << reg(rt) << ", "
                << shift << "U);";
            break;
        case 0x04:
            out << reg(rd) << " = " << reg(rt) << " << (" << reg(rs)
                << " & 31U);";
            break;
        case 0x06:
            if (shift == 1) {
                out << reg(rd) << " = rotate_right(" << reg(rt) << ", "
                    << reg(rs) << ");";
            } else if (shift == 0) {
                out << reg(rd) << " = " << reg(rt) << " >> (" << reg(rs)
                    << " & 31U);";
            } else {
                return "/* unsupported/reserved PSP word */ "
                       "state.stop_reason = StopReason::invalid_pc;";
            }
            break;
        case 0x07:
            out << reg(rd) << " = arithmetic_shift_right(" << reg(rt) << ", "
                << reg(rs) << ");";
            break;
        case 0x08:
            out << "state.branch_pending = true; state.branch_target = "
                << reg(rs) << ";";
            break;
        case 0x09:
            out << reg(rd) << " = state.memory_base + " << hex(pc + 8U)
                << "; state.branch_pending = true; state.branch_target = "
                << reg(rs) << ";";
            break;
        case 0x0a:
            out << "if (" << reg(rt) << " == 0) { " << reg(rd) << " = "
                << reg(rs) << "; }";
            break;
        case 0x0b:
            out << "if (" << reg(rt) << " != 0) { " << reg(rd) << " = "
                << reg(rs) << "; }";
            break;
        case 0x0c:
            out << "state.stop_reason = StopReason::syscall;";
            break;
        case 0x0d:
            // PPSSPP's default PSP compatibility behavior logs BREAK
            // exceptions and resumes at the following instruction.  Retail
            // games use this in fallback paths which still return an error to
            // their caller, so treating it as a terminal debugger stop can
            // kill an otherwise recoverable game thread.
            out << "/* break: ignored like PPSSPP's default exception policy */";
            break;
        case 0x0f:
            out << "/* sync: generated C++ executes in order */";
            break;
        case 0x10:
            out << reg(rd) << " = state.hi;";
            break;
        case 0x11:
            out << "state.hi = " << reg(rs) << ";";
            break;
        case 0x12:
            out << reg(rd) << " = state.lo;";
            break;
        case 0x13:
            out << "state.lo = " << reg(rs) << ";";
            break;
        case 0x16: // Allegrex clz
            out << reg(rd) << " = std::countl_zero(" << reg(rs) << ");";
            break;
        case 0x17: // Allegrex clo
            out << reg(rd) << " = std::countl_one(" << reg(rs) << ");";
            break;
        case 0x18:
            out << "{ const auto product = static_cast<std::int64_t>(as_s32("
                << reg(rs) << ")) * static_cast<std::int64_t>(as_s32("
                << reg(rt)
                << ")); state.lo = static_cast<std::uint32_t>(product); "
                   "state.hi = "
                   "static_cast<std::uint32_t>(static_cast<std::uint64_t>("
                   "product) "
                   ">> "
                   "32U); }";
            break;
        case 0x19:
            out << "{ const auto product = static_cast<std::uint64_t>("
                << reg(rs) << ") * static_cast<std::uint64_t>(" << reg(rt)
                << "); state.lo = static_cast<std::uint32_t>(product); "
                   "state.hi = "
                   "static_cast<std::uint32_t>(product >> 32U); }";
            break;
        case 0x1a:
            out << "{ const auto dividend = as_s32(" << reg(rs)
                << "); const auto divisor = as_s32(" << reg(rt)
                << "); if (divisor == 0) { state.lo = dividend < 0 ? 1U : "
                   "0xffffffffU; state.hi = "
                   "static_cast<std::uint32_t>(dividend); } "
                   "else if (static_cast<std::uint32_t>(dividend) == "
                   "0x80000000U && "
                   "divisor == -1) { state.lo = 0x80000000U; state.hi = 0xffffffffU; } "
                   "else { "
                   "state.lo = static_cast<std::uint32_t>(dividend / divisor); "
                   "state.hi "
                   "= static_cast<std::uint32_t>(dividend % divisor); } }";
            break;
        case 0x1b:
            out << "{ const auto divisor = " << reg(rt)
                << "; const auto dividend = " << reg(rs)
                << "; if (divisor == 0) { state.lo = dividend <= 0xffffU ? 0xffffU : 0xffffffffU; state.hi = dividend; } else { state.lo = dividend / divisor; state.hi = dividend % divisor; } }";
            break;
        case 0x1c: { // madd
            out << "{ const auto product = static_cast<std::int64_t>(as_s32("
                << reg(rs) << ")) * static_cast<std::int64_t>(as_s32("
                << reg(rt)
                << ")); const auto acc = static_cast<std::uint64_t>(state.lo) "
                   "| (static_cast<std::uint64_t>(state.hi) << 32U); "
                   "const auto result = static_cast<std::uint64_t>("
                   "static_cast<std::int64_t>(acc) + product); "
                   "state.lo = static_cast<std::uint32_t>(result); "
                   "state.hi = static_cast<std::uint32_t>(result >> 32U); }";
            break;
        }
        case 0x1d: { // maddu
            out << "{ const auto product = static_cast<std::uint64_t>("
                << reg(rs) << ") * static_cast<std::uint64_t>(" << reg(rt)
                << "); const auto acc = static_cast<std::uint64_t>(state.lo) "
                   "| (static_cast<std::uint64_t>(state.hi) << 32U); "
                   "const auto result = acc + product; "
                   "state.lo = static_cast<std::uint32_t>(result); "
                   "state.hi = static_cast<std::uint32_t>(result >> 32U); }";
            break;
        }
        case 0x20:
        case 0x21:
            out << reg(rd) << " = " << reg(rs) << " + " << reg(rt) << ";";
            break;
        case 0x22:
        case 0x23:
            out << reg(rd) << " = " << reg(rs) << " - " << reg(rt) << ";";
            break;
        case 0x24:
            out << reg(rd) << " = " << reg(rs) << " & " << reg(rt) << ";";
            break;
        case 0x25:
            out << reg(rd) << " = " << reg(rs) << " | " << reg(rt) << ";";
            break;
        case 0x26:
            out << reg(rd) << " = " << reg(rs) << " ^ " << reg(rt) << ";";
            break;
        case 0x27:
            out << reg(rd) << " = ~(" << reg(rs) << " | " << reg(rt) << ");";
            break;
        case 0x2a:
            out << reg(rd) << " = as_s32(" << reg(rs) << ") < as_s32("
                << reg(rt) << ");";
            break;
        case 0x2b:
            out << reg(rd) << " = " << reg(rs) << " < " << reg(rt) << ";";
            break;
        case 0x2c: // Allegrex max
            out << reg(rd) << " = as_s32(" << reg(rs) << ") > as_s32("
                << reg(rt) << ") ? " << reg(rs) << " : " << reg(rt) << ";";
            break;
        case 0x2d: // Allegrex min
            out << reg(rd) << " = as_s32(" << reg(rs) << ") < as_s32("
                << reg(rt) << ") ? " << reg(rs) << " : " << reg(rt) << ";";
            break;
        case 0x2e: { // msub
            out << "{ const auto product = static_cast<std::int64_t>(as_s32("
                << reg(rs) << ")) * static_cast<std::int64_t>(as_s32("
                << reg(rt)
                << ")); const auto acc = static_cast<std::uint64_t>(state.lo) "
                   "| (static_cast<std::uint64_t>(state.hi) << 32U); "
                   "const auto result = static_cast<std::uint64_t>("
                   "static_cast<std::int64_t>(acc) - product); "
                   "state.lo = static_cast<std::uint32_t>(result); "
                   "state.hi = static_cast<std::uint32_t>(result >> 32U); }";
            break;
        }
        case 0x2f: { // msubu
            out << "{ const auto product = static_cast<std::uint64_t>("
                << reg(rs) << ") * static_cast<std::uint64_t>(" << reg(rt)
                << "); const auto acc = static_cast<std::uint64_t>(state.lo) "
                   "| (static_cast<std::uint64_t>(state.hi) << 32U); "
                   "const auto result = acc - product; "
                   "state.lo = static_cast<std::uint32_t>(result); "
                   "state.hi = static_cast<std::uint32_t>(result >> 32U); }";
            break;
        }
        default:
            return "/* unsupported/reserved PSP word */ "
                   "state.stop_reason = StopReason::invalid_pc;";
        }
        return out.str();
    }

    switch (op) {
    case 0x01: {
        const bool ge = rt == 0x01 || rt == 0x03 || rt == 0x11 || rt == 0x13;
        const bool link = rt == 0x10 || rt == 0x11 || rt == 0x12 || rt == 0x13;
        const bool likely =
            rt == 0x02 || rt == 0x03 || rt == 0x12 || rt == 0x13;
        if (rt != 0x00 && rt != 0x01 && rt != 0x02 && rt != 0x03 &&
            rt != 0x10 && rt != 0x11 && rt != 0x12 && rt != 0x13) {
            out << "/* reserved REGIMM word in executable data */ "
                   "state.stop_reason = StopReason::invalid_pc;";
            break;
        }
        if (link) {
            out << "state.gpr[31] = state.memory_base + " << hex(pc + 8U)
                << "; ";
        }
        out << "if (as_s32(" << reg(rs) << ") " << (ge ? ">= 0" : "< 0")
            << ") { state.branch_pending = true; state.branch_target = "
            << branch_target(pc, instruction, relocated) << "; }";
        if (likely) {
            out << " else { state.pc = state.memory_base + " << hex(pc + 8U)
                << "; }";
        }
        break;
    }
    case 0x02:
    case 0x03: {
        if (op == 0x03) {
            out << "state.gpr[31] = state.memory_base + " << hex(pc + 8U)
                << "; ";
        }
        out << "state.branch_pending = true; state.branch_target = "
            << jump_target(pc, instruction, relocated, preferred_base) << ";";
        break;
    }
    case 0x04:
        out << "if (" << reg(rs) << " == " << reg(rt)
            << ") { state.branch_pending = true; state.branch_target = "
            << branch_target(pc, instruction, relocated) << "; }";
        break;
    case 0x05:
        out << "if (" << reg(rs) << " != " << reg(rt)
            << ") { state.branch_pending = true; state.branch_target = "
            << branch_target(pc, instruction, relocated) << "; }";
        break;
    case 0x06:
        out << "if (as_s32(" << reg(rs)
            << ") <= 0) { state.branch_pending = true; state.branch_target = "
            << branch_target(pc, instruction, relocated) << "; }";
        break;
    case 0x07:
        out << "if (as_s32(" << reg(rs)
            << ") > 0) { state.branch_pending = true; state.branch_target = "
            << branch_target(pc, instruction, relocated) << "; }";
        break;
    case 0x14:
        out << "if (" << reg(rs) << " == " << reg(rt)
            << ") { state.branch_pending = true; state.branch_target = "
            << branch_target(pc, instruction, relocated)
            << "; } else { state.pc = state.memory_base + " << hex(pc + 8U)
            << "; }";
        break;
    case 0x15:
        out << "if (" << reg(rs) << " != " << reg(rt)
            << ") { state.branch_pending = true; state.branch_target = "
            << branch_target(pc, instruction, relocated)
            << "; } else { state.pc = state.memory_base + " << hex(pc + 8U)
            << "; }";
        break;
    case 0x16:
        out << "if (as_s32(" << reg(rs)
            << ") <= 0) { state.branch_pending = true; state.branch_target = "
            << branch_target(pc, instruction, relocated)
            << "; } else { state.pc = state.memory_base + " << hex(pc + 8U)
            << "; }";
        break;
    case 0x17:
        out << "if (as_s32(" << reg(rs)
            << ") > 0) { state.branch_pending = true; state.branch_target = "
            << branch_target(pc, instruction, relocated)
            << "; } else { state.pc = state.memory_base + " << hex(pc + 8U)
            << "; }";
        break;
    case 0x1c:
        switch (function) {
        case 0x20: // clz
            out << reg(rd) << " = std::countl_zero(" << reg(rs) << ");";
            break;
        case 0x21: // clo
            out << reg(rd) << " = std::countl_one(" << reg(rs) << ");";
            break;
        case 0x2c: // max
            out << reg(rd) << " = as_s32(" << reg(rs) << ") > as_s32("
                << reg(rt) << ") ? " << reg(rs) << " : " << reg(rt) << ";";
            break;
        case 0x2d: // min
            out << reg(rd) << " = as_s32(" << reg(rs) << ") < as_s32("
                << reg(rt) << ") ? " << reg(rs) << " : " << reg(rt) << ";";
            break;
        default:
            return "/* unsupported/reserved PSP word */ "
                   "state.stop_reason = StopReason::invalid_pc;";
        }
        break;
    case 0x08:
    case 0x09:
        out << reg(rt) << " = " << reg(rs) << signed_imm(instruction, relocated)
            << ";";
        break;
    case 0x0a:
        out << reg(rt) << " = as_s32(" << reg(rs) << ") < "
            << signed_immediate_s32(instruction, relocated) << ";";
        break;
    case 0x0b:
        out << reg(rt) << " = " << reg(rs) << " < "
            << signed_immediate(instruction, relocated) << ";";
        break;
    case 0x0c:
        out << reg(rt) << " = " << reg(rs) << " & "
            << immediate(instruction, relocated) << ";";
        break;
    case 0x0d:
        out << reg(rt) << " = " << reg(rs) << " | "
            << immediate(instruction, relocated) << ";";
        break;
    case 0x0e:
        out << reg(rt) << " = " << reg(rs) << " ^ "
            << immediate(instruction, relocated) << ";";
        break;
    case 0x0f:
        out << reg(rt) << " = " << immediate(instruction, relocated)
            << " << 16U;";
        break;
    case 0x11: {
        const auto fmt = rs;
        const auto ft = rt;
        const auto fs = rd;
        const auto fd = shift;
        if (fmt == 0x00) {
            out << reg(rt) << " = state.fpr[" << fs << "];";
        } else if (fmt == 0x02) {
            if (fs == 0) {
                out << reg(rt) << " = 0x00003351U;";
            } else if (fs == 31) {
                out << reg(rt) << " = state.fcr31;";
            } else {
                out << reg(rt) << " = 0;";
            }
        } else if (fmt == 0x04) {
            out << "state.fpr[" << fs << "] = " << reg(rt) << ";";
        } else if (fmt == 0x06) {
            if (fs == 31) {
                out << "state.fcr31 = " << reg(rt) << ";";
            } else {
                out << "/* write to unimplemented FCR ignored */";
            }
        } else if (fmt == 0x08) {
            const auto cc = (rt >> 2U) & 7U;
            const bool branch_true = (rt & 1U) != 0U;
            const bool likely = (rt & 2U) != 0U;
            out << "if (fpu_condition(state, " << cc
                << "U) == " << (branch_true ? "true" : "false")
                << ") { state.branch_pending = true; state.branch_target = "
                << branch_target(pc, instruction, relocated) << "; }";
            if (likely) {
                out << " else { state.pc = state.memory_base + " << hex(pc + 8U)
                    << "; }";
            }
        } else if (fmt == 0x10) {
            switch (function) {
            case 0x00:
                out << "set_f32(state, " << fd << ", f32(state, " << fs
                    << ") + f32(state, " << ft << "));";
                break;
            case 0x01:
                out << "set_f32(state, " << fd << ", f32(state, " << fs
                    << ") - f32(state, " << ft << "));";
                break;
            case 0x02:
                out << "set_f32(state, " << fd << ", f32(state, " << fs
                    << ") * f32(state, " << ft << "));";
                break;
            case 0x03:
                out << "set_f32(state, " << fd << ", f32(state, " << fs
                    << ") / f32(state, " << ft << "));";
                break;
            case 0x04:
                out << "set_f32(state, " << fd << ", std::sqrt(f32(state, "
                    << fs << ")));";
                break;
            case 0x05:
                out << "state.fpr[" << fd << "] = state.fpr[" << fs
                    << "] & 0x7fffffffU;";
                break;
            case 0x06:
                out << "state.fpr[" << fd << "] = state.fpr[" << fs << "];";
                break;
            case 0x07:
                out << "state.fpr[" << fd << "] = state.fpr[" << fs
                    << "] ^ 0x80000000U;";
                break;
            case 0x0c:
                out << "state.fpr[" << fd << "] = rounded_word(f32(state, "
                    << fs << "), 0);";
                break;
            case 0x0d:
                out << "state.fpr[" << fd << "] = rounded_word(f32(state, "
                    << fs << "), 1);";
                break;
            case 0x0e:
                out << "state.fpr[" << fd << "] = rounded_word(f32(state, "
                    << fs << "), 2);";
                break;
            case 0x0f:
                out << "state.fpr[" << fd << "] = rounded_word(f32(state, "
                    << fs << "), 3);";
                break;
            case 0x11: {
                const auto cc = (ft >> 2U) & 7U;
                const bool move_true = (ft & 1U) != 0U;
                out << "if (fpu_condition(state, " << cc
                    << "U) == " << (move_true ? "true" : "false")
                    << ") { state.fpr[" << fd << "] = state.fpr[" << fs
                    << "]; }";
                break;
            }
            case 0x20:
                out << "state.fpr[" << fd << "] = state.fpr[" << fs << "];";
                break;
            case 0x24:
                out << "state.fpr[" << fd << "] = rounded_word(f32(state, "
                    << fs << "), state.fcr31 & 3U);";
                break;
            default:
                if (function >= 0x30) {
                    const auto cc = (instruction >> 8U) & 7U;
                    out << "compare_f32(state, " << cc << "U, f32(state, " << fs
                        << "), f32(state, " << ft << "), " << (function & 15U)
                        << "U);";
                } else {
                    return "/* unsupported/reserved PSP word */ "
                           "state.stop_reason = StopReason::invalid_pc;";
                }
                break;
            }
        } else if (fmt == 0x14 && function == 0x20) {
            out << "set_f32(state, " << fd << ", static_cast<float>(as_s32("
                << "state.fpr[" << fs << "])));";
        } else {
            return "/* unsupported/reserved PSP word */ "
                   "state.stop_reason = StopReason::invalid_pc;";
        }
        break;
    }
    case 0x20:
        out << reg(rt)
            << " = static_cast<std::uint32_t>(static_cast<std::int32_t>("
               "static_cast<std::int8_t>(PSPRECOMP_LOAD8(state, "
            << reg(rs) << signed_imm(instruction, relocated) << "))));";
        break;
    case 0x21:
        out << reg(rt)
            << " = static_cast<std::uint32_t>(static_cast<std::int32_t>("
               "static_cast<std::int16_t>(PSPRECOMP_LOAD16(state, "
            << reg(rs) << signed_imm(instruction, relocated) << "))));";
        break;
    case 0x22:
        out << reg(rt) << " = load_word_left(state, " << reg(rs)
            << signed_imm(instruction, relocated) << ", " << reg(rt) << ");";
        break;
    case 0x23:
        out << reg(rt) << " = PSPRECOMP_LOAD32(state, " << reg(rs)
            << signed_imm(instruction, relocated) << ");";
        break;
    case 0x24:
        out << reg(rt) << " = PSPRECOMP_LOAD8(state, " << reg(rs)
            << signed_imm(instruction, relocated) << ");";
        break;
    case 0x25:
        out << reg(rt) << " = PSPRECOMP_LOAD16(state, " << reg(rs)
            << signed_imm(instruction, relocated) << ");";
        break;
    case 0x26:
        out << reg(rt) << " = load_word_right(state, " << reg(rs)
            << signed_imm(instruction, relocated) << ", " << reg(rt) << ");";
        break;
    case 0x28:
        out << "PSPRECOMP_STORE8(state, " << reg(rs)
            << signed_imm(instruction, relocated)
            << ", static_cast<std::uint8_t>(" << reg(rt) << "));";
        break;
    case 0x29:
        out << "PSPRECOMP_STORE16(state, " << reg(rs)
            << signed_imm(instruction, relocated)
            << ", static_cast<std::uint16_t>(" << reg(rt) << "));";
        break;
    case 0x2a:
        out << "store_word_left(state, " << reg(rs)
            << signed_imm(instruction, relocated) << ", " << reg(rt) << ");";
        break;
    case 0x2b:
        out << "PSPRECOMP_STORE32(state, " << reg(rs)
            << signed_imm(instruction, relocated) << ", " << reg(rt) << ");";
        break;
    case 0x2e:
        out << "store_word_right(state, " << reg(rs)
            << signed_imm(instruction, relocated) << ", " << reg(rt) << ");";
        break;
    case 0x2f:
        out << "/* cache: no generated-runtime cache */";
        break;
    case 0x30: // ll - load linked (treat as lw on PSP user-mode)
        out << reg(rt) << " = PSPRECOMP_LOAD32(state, " << reg(rs)
            << signed_imm(instruction, relocated) << ");";
        break;
    case 0x38: // sc - store conditional (always succeeds on PSP user-mode)
        out << "PSPRECOMP_STORE32(state, " << reg(rs)
            << signed_imm(instruction, relocated) << ", " << reg(rt) << "); "
            << reg(rt) << " = 1U;";
        break;
    case 0x31:
        out << "state.fpr[" << rt << "] = PSPRECOMP_LOAD32(state, " << reg(rs)
            << signed_imm(instruction, relocated) << ");";
        break;
    case 0x39:
        out << "PSPRECOMP_STORE32(state, " << reg(rs)
            << signed_imm(instruction, relocated) << ", state.fpr[" << rt
            << "]);";
        break;
    case 0x1f: {
        if (function == 0x20 && shift == 0x14) {
            // bitrev
            out << reg(rd) << " = reverse_bits(" << reg(rt) << ");";
            break;
        }
        if (function == 0x20 && shift == 0x02) {
            // wsbh - swap bytes within halfwords
            out << reg(rd) << " = ((" << reg(rt)
                << " & 0xff00ff00U) >> 8U) | ((" << reg(rt)
                << " & 0x00ff00ffU) << 8U);";
            break;
        }
        if (function == 0x20 && shift == 0x03) {
            // wsbw - full byte swap
            out << reg(rd) << " = byte_swap(" << reg(rt) << ");";
            break;
        }
        if (function == 0x20 && shift == 0x10) {
            out << reg(rd)
                << " = static_cast<std::uint32_t>(static_cast<std::int32_t>("
                   "static_cast<std::int8_t>("
                << reg(rt) << ")));";
            break;
        }
        if (function == 0x20 && shift == 0x18) {
            out << reg(rd)
                << " = static_cast<std::uint32_t>(static_cast<std::int32_t>("
                   "static_cast<std::int16_t>("
                << reg(rt) << ")));";
            break;
        }
        if (function != 0) {
            if (function == 4 && rd >= shift) {
                const auto position = shift;
                const auto width = rd - shift + 1U;
                const auto mask = width == 32 ? 0xffffffffU
                                              : (1U << width) - 1U;
                const auto positioned_mask = mask << position;
                out << reg(rt) << " = (" << reg(rt) << " & ~"
                    << hex(positioned_mask) << ") | ((" << reg(rs) << " << "
                    << position << "U) & " << hex(positioned_mask) << ");";
                break;
            }
            return "/* unsupported/reserved PSP word */ "
                   "state.stop_reason = StopReason::invalid_pc;";
        }
        const auto position = shift;
        const auto width = rd + 1U;
        const auto mask = width == 32 ? 0xffffffffU : (1U << width) - 1U;
        out << reg(rt) << " = (" << reg(rs) << " >> " << position << "U) & "
            << hex(mask) << ";";
        break;
    }
    default:
        return "/* unsupported/reserved PSP word */ "
               "state.stop_reason = StopReason::invalid_pc;";
    }
    return out.str();
}

} // namespace

void emit_cpp(const ElfImage& image, const std::filesystem::path& output,
              const CodeMap* code_map) {
    (void)code_map;
    const auto relocated = relocated_words(image);
    // Decode everything first so a failed translation never leaves a partial
    // file.
    std::ostringstream cases;
    for (const auto& section : image.executable_sections) {
        for (std::size_t offset = 0; offset < section.bytes.size();
             offset += 4) {
            const auto pc =
                section.address + static_cast<std::uint32_t>(offset);
            const auto instruction = word_at(section, offset);
            cases << "        case " << hex(pc) << ": // " << hex(instruction)
                  << "\n            "
                  << emit_instruction(pc, instruction, relocated.contains(pc))
                  << "\n            break;\n";
        }
    }

    std::ofstream stream(output, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("cannot open output: " + output.string());
    }
    stream
        << "// Generated by psprecomp. Do not edit.\n"
           "#include <psprecomp/runtime.hpp>\n"
           "#include <psprecomp/vfpu.hpp>\n\n"
           "namespace psprecomp::generated {\n\n"
           "void run(State& state, std::uint32_t return_address, "
           "std::uint64_t max_steps) {\n"
           "    state.stop_reason = StopReason::running;\n";
    if (image.preferred_base != 0U) {
        stream << "    if (state.memory_base == 0U) state.memory_base = "
               << hex(image.preferred_base) << ";\n";
    }
    stream
        << "    for (std::uint64_t step = 0; step < max_steps; ++step) {\n"
           "        if (state.pc == return_address && !state.branch_pending) "
           "{\n"
           "            state.stop_reason = StopReason::returned;\n"
           "            return;\n"
           "        }\n"
           "        const bool apply_delayed_branch = state.branch_pending;\n"
           "        const std::uint32_t delayed_target = state.branch_target;\n"
           "        state.branch_pending = false;\n"
           "        const std::uint32_t current_pc = state.pc;\n"
           "        state.pc = current_pc + 4U;\n"
           "        using namespace psprecomp;\n"
           "        switch (current_pc - state.memory_base) {\n"
        << cases.str()
        << "        default:\n"
           "            state.stop_reason = StopReason::invalid_pc;\n"
           "            state.fault_address = current_pc;\n"
           "            return;\n"
           "        }\n"
           "        state.gpr[0] = 0;\n"
           "        if (state.stop_reason != StopReason::running) { return; }\n"
           "        if (apply_delayed_branch) { state.pc = delayed_target; }\n"
           "    }\n"
           "    state.stop_reason = StopReason::step_limit;\n"
           "}\n\n"
           "} // namespace psprecomp::generated\n";
    if (!stream) {
        throw std::runtime_error("failed while writing output: " +
                                 output.string());
    }
}

bool is_psp_sdk_stub(std::string_view symbol) {
  static const std::unordered_set<std::string_view> stubs = {
#define PSPSDK_STUB(name) #name,
#include "../refract/include/refract/psp_sdk_stubs.inc"
#undef PSPSDK_STUB
  };
  return stubs.contains(symbol);
}

void emit_project(const ElfImage& image, const std::filesystem::path& directory,
                  const CodeMap* code_map,
                  const GeneratedProjectOptions& options) {
    const auto shard_size = options.shard_size;
    if (shard_size == 0 || (shard_size & (shard_size - 1U)) != 0U ||
        shard_size < 0x1000U) {
        throw std::runtime_error("shard size must be a power of two >= 0x1000");
    }
    std::filesystem::create_directories(directory);
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        const auto name = entry.path().filename().string();
        const auto generated_code =
            name.rfind("func_", 0) == 0 || name.rfind("unit_", 0) == 0 ||
            name.rfind("shard_", 0) == 0;
        if (entry.is_regular_file() && entry.path().extension() == ".cpp" &&
            generated_code) {
            std::filesystem::remove(entry.path());
        }
    }
    const auto platform_directory = options.platform_directory.empty()
                                        ? directory / "platform"
                                        : options.platform_directory;
    std::filesystem::create_directories(platform_directory / "psp");
    std::filesystem::create_directories(platform_directory / "macos");

    struct PlatformImport {
        const Import* source;
        std::string symbol;
        std::string id;
        std::string psp_symbol;
    };
    std::vector<PlatformImport> platform_imports;
    platform_imports.reserve(image.imports.size());
    for (std::size_t index = 0; index < image.imports.size(); ++index) {
        const auto& import = image.imports[index];
        const auto symbol = import_symbol(import, code_map);
        if (symbol.empty()) {
            std::cout << "[UNMAPPED NID] lib=" << import.library
                      << " nid=0x" << std::hex << import.nid
                      << " stub=0x" << import.stub_address << std::dec << "\n";
        }
        std::ostringstream id;
        id << "import_" << std::setfill('0') << std::setw(4) << index << "_"
           << identifier(symbol.empty() ? import.library : symbol);
        platform_imports.push_back(
            {&import, symbol, id.str(), "psprecomp_psp_" + id.str()});
    }
    const auto native_relocation_bytes =
        image.preferred_base == 0U ? compact_relocations(image.relocations)
                                   : std::vector<std::uint8_t>{};
    std::size_t native_relocation_offset = image.memory_size();
    if (image.preferred_base == 0U) {
        std::size_t file_backed_end = 0;
        for (const auto& segment : image.load_segments) {
            file_backed_end =
                std::max(file_backed_end,
                         static_cast<std::size_t>(segment.address) +
                             segment.bytes.size());
        }
        if (file_backed_end <= image.memory_size() &&
            native_relocation_bytes.size() <=
                image.memory_size() - file_backed_end) {
            native_relocation_offset = file_backed_end;
        }
    }
    {
        std::ofstream stream(platform_directory / "platform.h",
                             std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("cannot create generated platform API");
        }
        stream
            << "// Generated by psprecomp. Implement this API for each host.\n"
               "#pragma once\n"
               "#include <psprecomp/runtime.hpp>\n"
               "#include <cstddef>\n"
               "#include <cstdint>\n\n"
               "namespace psprecomp::platform {\n"
               "enum class ImportId : std::uint16_t {\n";
        for (const auto& import : platform_imports) {
            stream << "  " << import.id << ", // "
                   << (import.symbol.empty() ? "unresolved" : import.symbol)
                   << " from " << import.source->library
                   << " (nid=" << hex(import.source->nid) << ")\n";
        }
        stream << "};\n\n"
                  "inline const char* import_name(ImportId id) {\n"
                  "  switch (id) {\n";
        for (const auto& import : platform_imports) {
            stream << "  case ImportId::" << import.id << ": return \""
                   << cpp_string(import.symbol.empty() ? import.source->library
                                                       : import.symbol)
                   << "\";\n";
        }
        stream << "  }\n"
                  "  return \"unknown PSP import\";\n"
                  "}\n\n"
                  "void configure_runtime(std::uint8_t* memory, "
                  "std::size_t size, std::uint32_t base);\n"
                  "void log(const char* format, std::uint32_t first = 0, "
                  "std::uint32_t second = 0);\n"
                  "bool patch_imports(State& state);\n"
                  "bool dispatch_import(State& state, "
                  "std::uint32_t current_pc);\n"
                  "std::uint32_t guest_gp_value();\n"
                  "} // namespace psprecomp::platform\n";
    }

    std::map<std::uint32_t, std::vector<EmittedInstruction>> shards;
    std::set<std::uint32_t> delay_slots;
    const auto relocated = relocated_words(image);
    const auto jump_bases = jump_relocation_bases(image);
    for (const auto& section : image.executable_sections) {
        for (std::size_t offset = 0; offset < section.bytes.size();
             offset += 4) {
            const auto pc =
                section.address + static_cast<std::uint32_t>(offset);
            const auto instruction = word_at(section, offset);
            shards[pc / shard_size].push_back(
                {pc, instruction,
                 emit_instruction(pc, instruction, relocated.contains(pc),
                                  image.preferred_base),
                 direct_control_target(pc, instruction, jump_bases,
                                       image.preferred_base)});
            if (starts_delayed_branch(instruction)) {
                delay_slots.insert(pc + 4U);
            }
        }
    }

    std::set<std::uint32_t> import_stubs;
    for (const auto& import : image.imports) {
        import_stubs.insert(import.stub_address);
    }

    // A code map lets project mode preserve real Guest function boundaries.
    // Keeping each Guest function as a separate C++ function gives the host
    // compiler a tractable control-flow graph and lets static J/JAL edges stay
    // on the native call stack. Each discovered Guest function is emitted into
    // its own source file so generated codebases remain easy to navigate.
    const bool function_mode =
        code_map != nullptr && !code_map->function_starts.empty();
    std::vector<std::uint32_t> mapped_function_starts;
    std::map<std::uint32_t, std::vector<EmittedInstruction>> functions;
    if (function_mode) {
        for (const auto address : code_map->function_starts) {
            if (is_executable_word(image, address)) {
                mapped_function_starts.push_back(address);
            }
        }
        std::sort(mapped_function_starts.begin(), mapped_function_starts.end());
        mapped_function_starts.erase(std::unique(mapped_function_starts.begin(),
                                                 mapped_function_starts.end()),
                                     mapped_function_starts.end());
        for (const auto& [unused_shard, instructions] : shards) {
            static_cast<void>(unused_shard);
            for (const auto& emitted : instructions) {
                const auto after =
                    std::upper_bound(mapped_function_starts.begin(),
                                     mapped_function_starts.end(), emitted.pc);
                if (after == mapped_function_starts.begin()) {
                    continue;
                }
                const auto owner = *std::prev(after);
                if (!import_stubs.contains(owner)) {
                    functions[owner].push_back(emitted);
                }
            }
        }
    }

    const auto function_owner =
        [&](std::uint32_t address) -> std::optional<std::uint32_t> {
        if (!function_mode) {
            return std::nullopt;
        }
        const auto after =
            std::upper_bound(mapped_function_starts.begin(),
                             mapped_function_starts.end(), address);
        if (after == mapped_function_starts.begin()) {
            return std::nullopt;
        }
        const auto owner = *std::prev(after);
        return functions.contains(owner) ? std::optional(owner) : std::nullopt;
    };

    const auto entry = code_map != nullptr ? code_map->entry : image.entry;
    const auto block_entries =
        potential_block_entries(image, code_map, entry, shard_size);

    struct PspOverlay {
        std::uint32_t start{};
        std::uint32_t end{};
        std::size_t index{};
        bool uses_vfpu{};
        std::size_t entry_route{};
    };
    struct PspOverlayRoute {
        std::size_t overlay_index{};
        std::uint32_t entry{};
        std::uint32_t link_register{};
        std::size_t index{};
        bool resume{};
        bool uses_vfpu{};
    };
    std::vector<PspOverlay> psp_overlays;
    std::vector<PspOverlayRoute> psp_overlay_routes;
    if (code_map != nullptr && !code_map->overlay_starts.empty()) {
        if (image.preferred_base != 0U) {
            throw std::runtime_error(
                "PSP overlays require a relocatable PRX image");
        }
        for (const auto start : code_map->overlay_starts) {
            const auto found = functions.find(start);
            if (found == functions.end() || found->second.size() < 2U) {
                throw std::runtime_error(
                    "PSP overlay is not a mapped function with at least "
                    "two instructions: " +
                    hex_address(start));
            }
            if (relocated.contains(start) || relocated.contains(start + 4U)) {
                throw std::runtime_error(
                    "PSP overlay entry detour would hide a relocated "
                    "instruction: " +
                    hex_address(start));
            }
            const auto end = found->second.back().pc + 4U;
            for (const auto& emitted : found->second) {
                const auto op = emitted.instruction >> 26U;
                const auto function = emitted.instruction & 0x3fU;
                const auto rs = (emitted.instruction >> 21U) & 0x1fU;
                const bool dynamic_jump =
                    op == 0U && function == 0x08U && rs != 31U;
                if (dynamic_jump) {
                    throw std::runtime_error(
                        "PSP overlay contains a dynamic non-return jump: " +
                        hex_address(start));
                }
            }
            const bool uses_vfpu = std::any_of(
                found->second.begin(), found->second.end(),
                [](const EmittedInstruction& emitted) {
                    return vfpu_opcode_supported(emitted.instruction);
                });
            psp_overlays.push_back(
                {start, end, psp_overlays.size(), uses_vfpu, 0U});
        }
        for (auto& overlay : psp_overlays) {
            overlay.entry_route = psp_overlay_routes.size();
            psp_overlay_routes.push_back(
                {overlay.index, overlay.start, 31U,
                 psp_overlay_routes.size(), false, overlay.uses_vfpu});
            std::map<std::uint32_t, std::uint32_t> continuations;
            for (const auto& emitted : functions.at(overlay.start)) {
                const auto op = emitted.instruction >> 26U;
                const auto function = emitted.instruction & 0x3fU;
                const auto rt = (emitted.instruction >> 16U) & 0x1fU;
                const auto rd = (emitted.instruction >> 11U) & 0x1fU;
                const bool links =
                    op == 0x03U ||
                    (op == 0U && function == 0x09U && rd != 0U) ||
                    (op == 0x01U && rt >= 0x10U && rt <= 0x13U);
                if (links) {
                    const auto link_register =
                        op == 0U ? rd : static_cast<std::uint32_t>(31U);
                    const auto [found, inserted] = continuations.emplace(
                        emitted.pc + 8U, link_register);
                    if (!inserted && found->second != link_register) {
                        throw std::runtime_error(
                            "PSP overlay continuation uses multiple link "
                            "registers: " +
                            hex_address(overlay.start));
                    }
                }
            }
            for (const auto& [continuation, link_register] : continuations) {
                if (continuation < overlay.start ||
                    continuation >= overlay.end) {
                    throw std::runtime_error(
                        "PSP overlay call continuation is outside its "
                        "mapped function: " +
                        hex_address(overlay.start));
                }
                psp_overlay_routes.push_back(
                    {overlay.index, continuation, link_register,
                     psp_overlay_routes.size(), true, overlay.uses_vfpu});
            }
        }
    }
    {
        std::ofstream header(directory / "generated.hpp",
                             std::ios::binary | std::ios::trunc);
        if (!header) {
            throw std::runtime_error("cannot create generated project header");
        }
        header << "// Generated by psprecomp. Do not edit.\n"
                  "#pragma once\n"
                  "#include <psprecomp/runtime.hpp>\n\n"
                  "namespace psprecomp::generated {\n"
                  "inline constexpr std::uint32_t guest_entry = "
               << hex(entry) << ";\n"
               << "inline constexpr std::uint32_t guest_memory_size = "
               << hex(image.memory_size()) << ";\n"
               << "inline constexpr std::uint32_t guest_gp_pointer_offset = "
               << hex(image.gp_pointer_offset) << ";\n";
        if (!function_mode) {
            for (const auto& [shard, unused] : shards) {
                static_cast<void>(unused);
                header << "bool run_shard_" << shard
                       << "(State& state, std::uint32_t entry_pc);\n";
            }
        }
        header
            << "bool dispatch_block(State& state, std::uint32_t current_pc);\n"
               "void run(State& state, std::uint32_t return_address, "
               "std::uint64_t max_steps);\n"
               "} // namespace psprecomp::generated\n";
    }

    std::vector<std::string> source_names;
    if (function_mode) {
        std::vector<std::uint32_t> native_starts;
        native_starts.reserve(functions.size());
        for (const auto& [start, unused] : functions) {
            static_cast<void>(unused);
            native_starts.push_back(start);
        }
        for (std::size_t first = 0; first < native_starts.size(); ++first) {
            const auto last = first + 1U;
            std::ostringstream name;
            name << "func_" << std::hex << std::setfill('0') << std::setw(8)
                 << native_starts[first] << ".cpp";
            source_names.push_back(name.str());
            std::ofstream stream(directory / name.str(),
                                 std::ios::binary | std::ios::trunc);
            if (!stream) {
                throw std::runtime_error(
                    "cannot create generated function source");
            }
            std::set<std::uint32_t> declarations;
            for (std::size_t index = first; index < last; ++index) {
                const auto start = native_starts[index];
                declarations.insert(start);
                for (const auto& emitted : functions.at(start)) {
                    if (!emitted.direct_target) {
                        continue;
                    }
                    const auto op = emitted.instruction >> 26U;
                    if ((op == 0x02U || op == 0x03U) &&
                        functions.contains(*emitted.direct_target)) {
                        declarations.insert(*emitted.direct_target);
                    }
                }
            }
            stream << "// Generated by psprecomp. Do not edit.\n"
                      "#include \"generated.hpp\"\n"
                      "#include <psprecomp/vfpu.hpp>\n\n"
                      "namespace psprecomp::generated {\n";
            for (const auto address : declarations) {
                stream << "bool " << function_name(address)
                       << "(State&, std::uint32_t);\n";
            }
            stream << "\n";

            for (std::size_t index = first; index < last; ++index) {
                const auto function_start = native_starts[index];
                const auto& instructions = functions.at(function_start);
                const auto function_end = instructions.back().pc + 4U;
                stream
                    << "// Original PSP binary range: ["
                    << hex_address(function_start) << ", "
                    << hex_address(function_end)
                    << ")\n"
                    << "bool " << function_name(function_start)
                    << "(State& __restrict__ state, std::uint32_t entry_pc) {\n"
                       "  using namespace psprecomp;\n"
                       "  bool apply_delayed_branch = state.branch_pending;\n"
                       "  std::uint32_t delayed_target = state.branch_target;\n"
                       "  state.branch_pending = false;\n"
                       "  switch (entry_pc - state.memory_base) {\n";
                for (const auto& emitted : instructions) {
                    if (block_entries.contains(emitted.pc)) {
                        stream << "  case " << hex(emitted.pc) << ": goto "
                               << pc_label(emitted.pc) << ";\n";
                    }
                }
                stream << "  default: return false;\n"
                          "  }\n";
                for (std::size_t i = 0; i < instructions.size(); ++i) {
                    const auto& emitted = instructions[i];
                    const bool has_next =
                        i + 1U < instructions.size() &&
                        instructions[i + 1U].pc == emitted.pc + 4U &&
                        !import_stubs.contains(instructions[i + 1U].pc);
                    const bool is_delay_slot = delay_slots.contains(emitted.pc);
                    const EmittedInstruction* delayed_control = nullptr;
                    if (is_delay_slot && i != 0U &&
                        instructions[i - 1U].pc + 4U == emitted.pc) {
                        delayed_control = &instructions[i - 1U];
                    }
                    const bool is_control =
                        starts_delayed_branch(emitted.instruction);
                    const bool may_stop =
                        may_stop_execution(emitted.instruction);
                    const bool writes_zero =
                        emitted.code.find("state.gpr[0] =") !=
                        std::string::npos;
                    const bool needs_current_pc =
                        emitted.code.find("current_pc") != std::string::npos;
                    if (block_entries.contains(emitted.pc)) {
                        stream << pc_label(emitted.pc) << ":;\n";
                    }
                    if (needs_current_pc) {
                        stream << "  {\n"
                               << "    const std::uint32_t current_pc = "
                                  "state.memory_base + "
                               << hex(emitted.pc) << ";\n";
                    }
                    if (is_delay_slot || is_control || may_stop || !has_next) {
                        stream << (needs_current_pc ? "    " : "  ")
                               << "state.pc = state.memory_base + "
                               << hex(emitted.pc + 4U) << ";\n";
                    }
                    stream << (needs_current_pc ? "    " : "  ") << emitted.code
                           << "\n";
                    if (writes_zero) {
                        stream << (needs_current_pc ? "    " : "  ")
                               << "state.gpr[0] = 0;\n";
                    }
                    if (needs_current_pc) {
                        stream << "  }\n";
                    }
                    if (may_stop) {
                        stream << "  if (state.stop_reason != "
                                  "StopReason::running) "
                                  "return true;\n";
                    }
                    if (is_delay_slot) {
                        stream << "  if (apply_delayed_branch) {\n"
                                  "    state.pc = delayed_target;\n";
                        if (delayed_control != nullptr &&
                            delayed_control->direct_target) {
                            const auto target = *delayed_control->direct_target;
                            const auto op = delayed_control->instruction >> 26U;
                            if (op == 0x03U && functions.contains(target)) {
                                stream
                                    << "#ifndef PSPRECOMP_PSP_OVERLAY\n"
                                    << "    if (delayed_target == "
                                       "state.memory_base + "
                                    << hex(target)
                                    << ") {\n"
                                       "      apply_delayed_branch = false;\n"
                                       "      note_cpu_direct_cfg_edge(state);\n"
                                    << "      if (!" << function_name(target)
                                    << "(state, delayed_target)) return "
                                       "false;\n"
                                       "      if (state.stop_reason != "
                                       "StopReason::running) return true;\n";
                                const auto return_pc = emitted.pc + 4U;
                                if (function_owner(return_pc) ==
                                        function_start &&
                                    block_entries.contains(return_pc)) {
                                    stream
                                        << "      if (!state.branch_pending && "
                                           "state.pc == state.memory_base + "
                                        << hex(return_pc) << ") goto "
                                        << pc_label(return_pc) << ";\n";
                                }
                                stream << "      return true;\n"
                                          "    }\n"
                                          "#endif\n";
                            } else if (op == 0x02U &&
                                       functions.contains(target) &&
                                       target != function_start) {
                                stream
                                    << "#ifndef PSPRECOMP_PSP_OVERLAY\n"
                                    << "    if (delayed_target == "
                                       "state.memory_base + "
                                    << hex(target)
                                    << ") {\n"
                                       "      apply_delayed_branch = false;\n"
                                       "      note_cpu_direct_cfg_edge(state);\n"
                                    << "      return " << function_name(target)
                                    << "(state, delayed_target);\n"
                                       "    }\n"
                                       "#endif\n";
                            }
                            if (function_owner(target) == function_start &&
                                block_entries.contains(target)) {
                                stream
                                    << "    if (delayed_target == "
                                       "state.memory_base + "
                                    << hex(target)
                                    << ") {\n"
                                       "      apply_delayed_branch = false;\n"
                                       "      note_cpu_direct_cfg_edge(state);\n"
                                       "      goto "
                                    << pc_label(target)
                                    << ";\n"
                                       "    }\n";
                            }
                        }
                        stream << "    return true;\n"
                                  "  }\n";
                    }
                    if (is_control) {
                        stream << "  if (state.pc != state.memory_base + "
                               << hex(emitted.pc + 4U) << ") return true;\n";
                        if (has_next) {
                            stream
                                << "  apply_delayed_branch = "
                                   "state.branch_pending;\n"
                                   "  delayed_target = state.branch_target;\n"
                                   "  state.branch_pending = false;\n";
                        }
                    }
                    if (!has_next) {
                        stream << "  return true;\n";
                    }
                }
                stream << "}\n\n";
            }
            stream << "} // namespace psprecomp::generated\n";
        }
    } else {
        for (const auto& [shard, instructions] : shards) {
            std::ostringstream name;
            name << "shard_" << std::hex << std::setfill('0') << std::setw(4)
                 << shard << ".cpp";
            source_names.push_back(name.str());
            std::ofstream stream(directory / name.str(),
                                 std::ios::binary | std::ios::trunc);
            if (!stream) {
                throw std::runtime_error("cannot create generated shard");
            }
            stream << "// Generated by psprecomp. Do not edit.\n"
                      "#include \"generated.hpp\"\n"
                      "#include <psprecomp/vfpu.hpp>\n\n"
                      "namespace psprecomp::generated {\n"
                      "bool run_shard_"
                   << shard
                   << "(State& __restrict__ state, std::uint32_t entry_pc) {\n"
                      "  using namespace psprecomp;\n"
                      "  bool apply_delayed_branch = state.branch_pending;\n"
                      "  std::uint32_t delayed_target = state.branch_target;\n"
                      "  state.branch_pending = false;\n"
                      "  switch (entry_pc - state.memory_base) {\n";
            for (const auto& emitted : instructions) {
                if (block_entries.contains(emitted.pc)) {
                    stream << "  case " << hex(emitted.pc) << ": goto "
                           << pc_label(emitted.pc) << ";\n";
                }
            }
            stream << "  default: return false;\n"
                      "  }\n";
            for (std::size_t i = 0; i < instructions.size(); ++i) {
                const auto& emitted = instructions[i];
                const bool has_next =
                    i + 1U < instructions.size() &&
                    instructions[i + 1U].pc == emitted.pc + 4U &&
                    !import_stubs.contains(instructions[i + 1U].pc);
                const bool is_delay_slot = delay_slots.contains(emitted.pc);
                std::optional<std::uint32_t> delayed_direct_target;
                if (is_delay_slot && i != 0U &&
                    instructions[i - 1U].pc + 4U == emitted.pc) {
                    delayed_direct_target = instructions[i - 1U].direct_target;
                }
                const bool is_control =
                    starts_delayed_branch(emitted.instruction);
                const bool may_stop = may_stop_execution(emitted.instruction);
                const bool writes_zero =
                    emitted.code.find("state.gpr[0] =") != std::string::npos;
                const bool needs_current_pc =
                    emitted.code.find("current_pc") != std::string::npos;
                if (block_entries.contains(emitted.pc)) {
                    stream << pc_label(emitted.pc) << ":;\n";
                }
                if (needs_current_pc) {
                    stream << "  {\n"
                           << "    const std::uint32_t current_pc = "
                              "state.memory_base + "
                           << hex(emitted.pc) << ";\n";
                }
                if (is_delay_slot || is_control || may_stop || !has_next) {
                    stream << (needs_current_pc ? "    " : "  ")
                           << "state.pc = state.memory_base + "
                           << hex(emitted.pc + 4U) << ";\n";
                }
                stream << (needs_current_pc ? "    " : "  ") << emitted.code
                       << "\n";
                if (writes_zero) {
                    stream << (needs_current_pc ? "    " : "  ")
                           << "state.gpr[0] = 0;\n";
                }
                if (needs_current_pc) {
                    stream << "  }\n";
                }
                if (may_stop) {
                    stream << "  if (state.stop_reason != StopReason::running) "
                              "return true;\n";
                }
                if (is_delay_slot) {
                    stream << "  if (apply_delayed_branch) {\n"
                              "    state.pc = delayed_target;\n";
                    if (delayed_direct_target &&
                        *delayed_direct_target / shard_size == shard &&
                        block_entries.contains(*delayed_direct_target) &&
                        !import_stubs.contains(*delayed_direct_target)) {
                        stream
                            << "    if (delayed_target == state.memory_base + "
                            << hex(*delayed_direct_target)
                            << ") {\n"
                               "      apply_delayed_branch = false;\n"
                               "      note_cpu_direct_cfg_edge(state);\n"
                               "      goto "
                            << pc_label(*delayed_direct_target)
                            << ";\n"
                               "    }\n";
                    }
                    stream << "    return true;\n"
                              "  }\n";
                }
                if (is_control) {
                    stream << "  if (state.pc != state.memory_base + "
                           << hex(emitted.pc + 4U) << ") return true;\n";
                    if (has_next) {
                        stream << "  apply_delayed_branch = "
                                  "state.branch_pending;\n"
                                  "  delayed_target = state.branch_target;\n"
                                  "  state.branch_pending = false;\n";
                    }
                }
                if (!has_next) {
                    stream << "  return true;\n";
                }
            }
            stream << "}\n"
                      "} // namespace psprecomp::generated\n";
        }
    }

    {
        std::ofstream stream(platform_directory / "psp" / "platform.cpp",
                             std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("cannot create generated import bridge");
        }
        stream << "// Generated by psprecomp. Do not edit.\n"
                  "#include \"generated.hpp\"\n"
                  "#include <platform/platform.h>\n"
                  "#include <algorithm>\n"
                  "#include <cstddef>\n"
                  "#include <cstdint>\n"
                  "#include <cstdlib>\n"
                  "#include <malloc.h>\n\n"
                  "using PspThreadEntry = int (*)(std::uint32_t, void*);\n"
                  "extern \"C\" int sceKernelCreateThread(const char*, "
                  "PspThreadEntry, int, int, std::uint32_t, void*);\n";
        for (const auto& import : platform_imports) {
            stream << "extern \"C\" std::uint32_t " << import.psp_symbol
                   << "(...);\n";
        }
        stream << "extern \"C\" int sceKernelPrintf(const char*, ...);\n";
        stream << "\nnamespace psprecomp::platform {\n"
                  "void log(const char* format, std::uint32_t first, "
                  "std::uint32_t second) {\n"
                  "  sceKernelPrintf(format, first, second);\n"
                  "}\n\n"
                  "namespace {\n"
                  "std::uint8_t* shared_memory;\n"
                  "std::size_t shared_memory_size;\n"
                  "std::uint32_t shared_memory_base;\n"
                  "std::uint32_t shared_memory_gp;\n"
                  "using PspSdkFunction = std::uint32_t (*)(...);\n"
                  "std::uint32_t stack_argument(const State& state, "
                  "std::size_t index) {\n"
                  "  const auto address = canonical_address(state.gpr[29] + "
                  "16U + static_cast<std::uint32_t>(index * 4U));\n"
                  "  if (const auto* pointer = mapped_address(state, address, "
                  "4U)) return static_cast<std::uint32_t>(pointer[0]) | "
                  "(static_cast<std::uint32_t>(pointer[1]) << 8U) | "
                  "(static_cast<std::uint32_t>(pointer[2]) << 16U) | "
                  "(static_cast<std::uint32_t>(pointer[3]) << 24U);\n"
                  "  if (!state.direct_memory_access || (address & 3U) != 0U "
                  "|| address < 0x00010000U) return 0U;\n"
                  "  const auto* bytes = reinterpret_cast<const volatile "
                  "std::uint8_t*>(static_cast<std::uintptr_t>(address));\n"
                  "  return static_cast<std::uint32_t>(bytes[0]) | "
                  "(static_cast<std::uint32_t>(bytes[1]) << 8U) | "
                  "(static_cast<std::uint32_t>(bytes[2]) << 16U) | "
                  "(static_cast<std::uint32_t>(bytes[3]) << 24U);\n"
                  "}\n"
                  "std::uint32_t call_sdk_api(PspSdkFunction function, "
                  "const State& state) {\n"
                  "  return function(state.gpr[4], state.gpr[5], "
                  "state.gpr[6], state.gpr[7], state.gpr[8], state.gpr[9], "
                  "state.gpr[10], state.gpr[11], stack_argument(state, 0), "
                  "stack_argument(state, 1), stack_argument(state, 2), "
                  "stack_argument(state, 3), stack_argument(state, 4), "
                  "stack_argument(state, 5), stack_argument(state, 6), "
                  "stack_argument(state, 7));\n"
                  "}\n"
                  "struct ThreadSlot { std::uint32_t entry; std::uint32_t "
                  "stack_size; std::uint8_t* guest_stack; bool used; };\n"
                  "ThreadSlot thread_slots[32]{};\n"
                  "constexpr std::uint32_t thread_return_address = "
                  "0xfffffff0U;\n\n"
                  "template <std::size_t Slot>\n"
                  "int run_guest_thread(std::uint32_t args, void* argp) {\n"
                  "  std::uint32_t native_k0;\n"
                  "  std::uint32_t native_k1;\n"
                  "  asm volatile(\"move %0, $26\" : \"=r\"(native_k0));\n"
                  "  asm volatile(\"move %0, $27\" : \"=r\"(native_k1));\n"
                  "  auto& slot = thread_slots[Slot];\n"
                  "  sceKernelPrintf(\"[psprecomp] guest thread=%u "
                  "entry=%08x\\n\", static_cast<unsigned int>(Slot), "
                  "static_cast<unsigned int>(slot.entry - "
                  "shared_memory_base));\n"
                  "  State state;\n"
                  "  state.memory = shared_memory;\n"
                  "  state.memory_size = shared_memory_size;\n"
                  "  state.memory_base = shared_memory_base;\n"
                  "  state.direct_memory_access = true;\n"
                  "  state.pc = slot.entry;\n"
                  "  state.gpr[4] = args;\n"
                  "  state.gpr[5] = static_cast<std::uint32_t>("
                  "reinterpret_cast<std::uintptr_t>(argp));\n"
                  "  state.gpr[26] = native_k0;\n"
                  "  state.gpr[27] = native_k1;\n"
                  "  state.gpr[29] = static_cast<std::uint32_t>("
                  "reinterpret_cast<std::uintptr_t>(slot.guest_stack + "
                  "slot.stack_size - 64U));\n"
                  "  state.gpr[31] = thread_return_address;\n"
                  "  state.gpr[28] = shared_memory_gp;\n"
                  "  asm volatile(\"move $gp, %0\" :: \"r\"(shared_memory_gp) "
                  ");\n"
                  "  generated::run(state, thread_return_address, "
                  "0x7fffffffffffffffULL);\n"
                  "  sceKernelPrintf(\"[psprecomp] guest stopped slot=%u "
                  "stop=%u pc=%08x\\n\", static_cast<unsigned int>(Slot), "
                  "static_cast<unsigned int>(state.stop_reason), "
                  "static_cast<unsigned int>(state.pc - "
                  "shared_memory_base));\n"
                  "  sceKernelPrintf(\"[psprecomp] guest fault=%08x "
                  "pc=%08x\\n\", static_cast<unsigned int>("
                  "state.fault_address), static_cast<unsigned int>("
                  "state.fault_pc - shared_memory_base));\n"
                  "  sceKernelPrintf(\"[psprecomp] guest insn=%08x "
                  "gp=%08x\\n\", static_cast<unsigned int>("
                  "state.fault_instruction), static_cast<unsigned int>("
                  "state.gpr[28]));\n"
                  "  sceKernelPrintf(\"[psprecomp] guest ra=%08x "
                  "sp=%08x\\n\", static_cast<unsigned int>("
                  "state.gpr[31] - shared_memory_base), "
                  "static_cast<unsigned int>(state.gpr[29]));\n"
                  "  sceKernelPrintf(\"[psprecomp] guest a0=%08x "
                  "a1=%08x\\n\", static_cast<unsigned int>(state.gpr[4]), "
                  "static_cast<unsigned int>(state.gpr[5]));\n"
                  "  const auto result = static_cast<int>(state.gpr[2]);\n"
                  "  return result;\n"
                  "}\n\n"
                  "PspThreadEntry thread_entries[32] = {\n";
        for (std::size_t i = 0; i < 32; ++i) {
            stream << "  &run_guest_thread<" << i << ">"
                   << (i + 1 == 32 ? "\n" : ",\n");
        }
        stream << "};\n\n"
                  "bool create_guest_thread(State& state) {\n"
                  "  std::size_t slot = 0;\n"
                  "  while (slot < 32 && thread_slots[slot].used) { ++slot; }\n"
                  "  if (slot == 32) { state.gpr[2] = 0x80020190U; return "
                  "true; }\n"
                  "  const std::uint32_t stack_size = std::max<std::uint32_t>("
                  "state.gpr[7], 256);\n"
                  "  auto* guest_stack = static_cast<std::uint8_t*>("
                  "memalign(64, stack_size));\n"
                  "  if (guest_stack == nullptr) { state.gpr[2] = "
                  "0x80020190U; return true; }\n"
                  "  thread_slots[slot] = {state.gpr[5], stack_size, "
                  "guest_stack, true};\n"
                  "  const auto attr = state.gpr[8];\n"
                  "  auto* option = state.gpr[9] == 0U ? nullptr : "
                  "psprecomp::mapped_address(state, state.gpr[9], 1U);\n"
                  "  auto* name = reinterpret_cast<const char*>("
                  "psprecomp::mapped_address(state, state.gpr[4], 1U));\n"
                  "  sceKernelPrintf(\"[psprecomp] create guest slot=%u "
                  "entry=%08x\\n\", static_cast<unsigned int>(slot), "
                  "static_cast<unsigned int>(state.gpr[5] - "
                  "state.memory_base));\n"
                  "  state.gpr[2] = static_cast<std::uint32_t>("
                  "sceKernelCreateThread(name, "
                  "thread_entries[slot], static_cast<int>(state.gpr[6]), "
                  "static_cast<int>(std::max<std::uint32_t>(state.gpr[7], "
                  "0x10000U)), attr, option));\n"
                  "  if (static_cast<std::int32_t>(state.gpr[2]) < 0) { "
                  "std::free(guest_stack); thread_slots[slot].guest_stack = "
                  "nullptr; thread_slots[slot].used = false; }\n"
                  "  return true;\n"
                  "}\n"
                  "} // namespace\n\n"
                  "void configure_runtime(std::uint8_t* memory, "
                  "std::size_t size, std::uint32_t base) {\n"
                  "  shared_memory = memory; shared_memory_size = size; "
                  "shared_memory_base = base;\n"
                  "  shared_memory_gp = generated::guest_gp_pointer_offset "
                  "!= 0xffffffffU && generated::guest_gp_pointer_offset <= "
                  "size - 4U ? (static_cast<std::uint32_t>("
                  "memory[generated::guest_gp_pointer_offset]) | "
                  "(static_cast<std::uint32_t>(memory["
                  "generated::guest_gp_pointer_offset + 1U]) << 8U) | "
                  "(static_cast<std::uint32_t>(memory["
                  "generated::guest_gp_pointer_offset + 2U]) << 16U) | "
                  "(static_cast<std::uint32_t>(memory["
                  "generated::guest_gp_pointer_offset + 3U]) << 24U)) : 0U;\n"
                  "}\n\n"
                  "std::uint32_t guest_gp_value() { return "
                  "shared_memory_gp; }\n\n"
                  "bool patch_imports(State& state) {\n";
        for (const auto& import : platform_imports) {
            stream << "  { const auto target = static_cast<std::uint32_t>("
                      "reinterpret_cast<std::uintptr_t>(&"
                   << import.psp_symbol
                   << ")); const auto stub = state.memory_base + "
                   << hex(import.source->stub_address)
                   << "; if (((stub + 4U) & 0xf0000000U) != "
                      "(target & 0xf0000000U)) return false; "
                      "store32(state, stub, 0x08000000U | "
                      "((target >> 2U) & 0x03ffffffU)); }\n";
        }
        stream << "  return state.stop_reason == StopReason::running;\n"
                  "}\n\n"
                  "bool dispatch_import(State& state, std::uint32_t "
                  "current_pc) {\n"
                  "  switch (current_pc - state.memory_base) {\n";
        for (const auto& import : platform_imports) {
            stream << "  case " << hex(import.source->stub_address) << ": ";
            if (import.symbol == "sceKernelCreateThread") {
                // Route thread creation through create_guest_thread so the
                // new thread's real native entry point is our own
                // run_guest_thread<Slot> trampoline (which re-enters the
                // translated interpreter), instead of forwarding the raw
                // guest PC to the real SDK function. Without this, the OS
                // would later jump directly into the untranslated original
                // guest machine code for every spawned thread, bypassing
                // psprecomp's C++ translation (and its virtual $gp/register
                // state) entirely.
                stream << "create_guest_thread(state); ";
            } else {
                stream << "state.gpr[2] = call_sdk_api("
                          "reinterpret_cast<PspSdkFunction>("
                          "reinterpret_cast<void*>(&"
                       << import.psp_symbol << ")), state); ";
            }
            stream << "state.pc = state.gpr[31]; return true;\n";
        }
        stream << "  default: return false;\n"
                  "  }\n"
                  "}\n"
                  "} // namespace psprecomp::platform\n";
    }

    source_names.push_back("dispatch.cpp");
    {
        std::ofstream stream(directory / "dispatch.cpp",
                             std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("cannot create generated dispatcher");
        }
        stream
            << "// Generated by psprecomp. Do not edit.\n"
               "#include \"generated.hpp\"\n"
               "#include <platform/platform.h>\n"
               "#include <psprecomp/interpreter.hpp>\n"
               "\n"
               "namespace psprecomp::generated {\n"
               "#ifdef PSPRECOMP_PROFILE_DISPATCH\n"
               "namespace {\n"
               "void profile_report(const State& state, std::uint32_t pc) {\n"
               "  if (!state.cpu_profile_enabled || "
               "(state.cpu_profile.dispatches & 0x000fffffU) != 0U) return;\n"
               "  const auto& p = state.cpu_profile;\n"
               "  platform::log(\"[psprecomp-profile] pc=%08x "
               "dispatches=%u\\n\", pc, "
               "static_cast<std::uint32_t>(p.dispatches));\n"
               "  platform::log(\"[psprecomp-profile] translated=%u "
               "fallback=%u\\n\", "
               "static_cast<std::uint32_t>(p.translated_blocks), "
               "static_cast<std::uint32_t>(p.interpreter_fallbacks));\n"
               "  platform::log(\"[psprecomp-profile] direct=%u "
               "reads=%u\\n\", "
               "static_cast<std::uint32_t>(p.direct_cfg_edges), "
               "static_cast<std::uint32_t>(p.memory_reads));\n"
               "  platform::log(\"[psprecomp-profile] writes=%u "
               "faults=%u\\n\", "
               "static_cast<std::uint32_t>(p.memory_writes), "
               "static_cast<std::uint32_t>(p.memory_faults));\n"
               "}\n"
               "} // namespace\n"
               "#endif\n";
        if (function_mode) {
            for (const auto& [start, unused] : functions) {
                static_cast<void>(unused);
                stream << "bool " << function_name(start)
                       << "(State&, std::uint32_t);\n";
            }
            stream
                << "namespace {\n"
                   "using FunctionRunner = bool (*)(State&, std::uint32_t);\n"
                   "struct FunctionRoute { std::uint32_t start; "
                   "FunctionRunner runner; };\n"
                   "constexpr FunctionRoute function_routes[] = {\n";
            for (const auto& [start, unused] : functions) {
                static_cast<void>(unused);
                stream << "  {" << hex(start) << ", &" << function_name(start)
                       << "},\n";
            }
            stream << "};\n"
                      "} // namespace\n";
        } else {
            for (const auto& [shard, instructions] : shards) {
                (void)instructions;
                stream << "bool run_shard_" << shard
                       << "(State&, std::uint32_t);\n";
            }
        }
        stream
            << "bool dispatch_block(State& state, std::uint32_t current_pc) {\n"
               "  if (!state.branch_pending && "
               "platform::dispatch_import(state, current_pc)) "
               "return true;\n";
        if (function_mode) {
            stream << "  const auto offset = current_pc - state.memory_base;\n"
                      "  constexpr std::size_t route_count = "
                      "sizeof(function_routes) / sizeof(function_routes[0]);\n"
                      "  const auto hint = "
                      "static_cast<std::size_t>(state.dispatch_route_hint);\n"
                      "  if (hint < route_count && "
                      "function_routes[hint].start <= offset && "
                      "(hint + 1U == route_count || "
                      "offset < function_routes[hint + 1U].start)) {\n"
                      "    return function_routes[hint].runner(state, "
                      "current_pc);\n"
                      "  }\n"
                      "  std::size_t first = 0;\n"
                      "  std::size_t last = route_count;\n"
                      "  while (first < last) {\n"
                      "    const auto middle = first + (last - first) / 2U;\n"
                      "    if (function_routes[middle].start <= offset) "
                      "first = middle + 1U; else last = middle;\n"
                      "  }\n"
                      "  if (first == 0) return false;\n"
                      "  state.dispatch_route_hint = "
                      "static_cast<std::uint32_t>(first - 1U);\n"
                      "  const auto& route = "
                      "function_routes[state.dispatch_route_hint];\n"
                      "  return route.runner(state, current_pc);\n";
        } else {
            stream << "  switch ((current_pc - state.memory_base) / "
                   << shard_size << "U) {\n";
            for (const auto& [shard, instructions] : shards) {
                (void)instructions;
                stream << "  case " << shard << "U: return run_shard_" << shard
                       << "(state, current_pc);\n";
            }
            stream << "  default: return false;\n"
                      "  }\n";
        }
        stream
            << "}\n\n"
               "void run(State& state, std::uint32_t return_address, "
               "std::uint64_t max_steps) {\n"
               "  state.stop_reason = StopReason::running;\n"
               "  for (std::uint64_t block = 0; block < max_steps; ++block) {\n"
               "    if (state.pc == return_address && !state.branch_pending) "
               "{\n"
               "      state.stop_reason = StopReason::returned; return;\n"
               "    }\n"
               "    const std::uint32_t current_pc = state.pc;\n"
               "    note_cpu_dispatch(state);\n"
               "    const bool translated = dispatch_block(state, "
               "current_pc);\n"
               "    if (translated) {\n"
               "      note_cpu_translated_block(state);\n"
               "    } else {\n"
               "      note_cpu_interpreter_fallback(state);\n"
               "    }\n"
               "#ifdef PSPRECOMP_PROFILE_DISPATCH\n"
               "    profile_report(state, current_pc);\n"
               "#endif\n"
               "    if (!translated && !interpret_allegrex(state, "
               "current_pc)) {\n"
               "      state.stop_reason = StopReason::invalid_pc;\n"
               "      state.fault_address = current_pc; return;\n"
               "    }\n"
               "    if (state.stop_reason != StopReason::running) { return; }\n"
               "  }\n"
               "  state.stop_reason = StopReason::step_limit;\n"
               "}\n"
               "} // namespace psprecomp::generated\n";
    }

    {
        const auto memory = image.memory_image();
        std::ofstream stream(directory / "guest_image.bin",
                             std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("cannot create guest memory image");
        }
        if (!memory.empty()) {
            stream.write(reinterpret_cast<const char*>(memory.data()),
                         static_cast<std::streamsize>(memory.size()));
        }
    }
    {
        auto memory = image.memory_image();
        if (native_relocation_offset > memory.size()) {
            throw std::runtime_error("invalid native relocation offset");
        }
        if (native_relocation_offset == memory.size()) {
            memory.insert(memory.end(), native_relocation_bytes.begin(),
                          native_relocation_bytes.end());
        } else {
            if (native_relocation_bytes.size() >
                memory.size() - native_relocation_offset) {
                throw std::runtime_error(
                    "native relocations overlap the guest image");
            }
            std::copy(native_relocation_bytes.begin(),
                      native_relocation_bytes.end(),
                      memory.begin() + native_relocation_offset);
        }
        std::ofstream stream(directory / "native_guest_image.bin",
                             std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("cannot create native guest image");
        }
        if (!memory.empty()) {
            stream.write(reinterpret_cast<const char*>(memory.data()),
                         static_cast<std::streamsize>(memory.size()));
        }
    }
    {
        std::ofstream stream(platform_directory / "psp" / "main.cpp",
                             std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("cannot create generated PSP entry point");
        }
        if (image.preferred_base != 0U) {
            stream << "// Generated by psprecomp. Do not edit.\n"
                      "#include \"generated.hpp\"\n"
                      "#include <platform/platform.h>\n"
                      "#include <psprecomp/relocation.hpp>\n"
                      "#include <pspkernel.h>\n"
                      "#include <cstddef>\n"
                      "#include <cstdint>\n"
                      "#include <cstring>\n"
                      "#include <malloc.h>\n\n"
                      "PSP_MODULE_INFO(\""
                   << cpp_string(options.module_name)
                   << "\", 0, 1, 0);\n"
                      "PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | "
                      "THREAD_ATTR_VFPU);\n"
                      "PSP_MAIN_THREAD_STACK_SIZE_KB(256);\n"
                      "PSP_HEAP_SIZE_KB(1024);\n\n"
                      "PSP_NO_CREATE_MAIN_THREAD();\n\n"
                      "extern \"C\" unsigned char guest_image_start[];\n"
                      "extern \"C\" unsigned char "
                      "guest_relocations_start[];\n\n"
                      "namespace {\n"
                      "constexpr psprecomp::PspLoadSegment segments[] = {\n";
            for (const auto& segment : image.load_segments) {
                stream << "  {" << hex(segment.address) << ", "
                       << hex(segment.memory_size) << ", "
                       << static_cast<unsigned>(segment.program_index)
                       << "},\n";
            }
            stream
                << "};\n"
                   "constexpr std::size_t main_stack_size = 0x40000U;\n"
                   "constexpr std::uint32_t return_address = 0xfffffff0U;\n"
                   "} // namespace\n\n"
                   "int main(int argc, char** argv) {\n"
                   "  sceKernelPrintf(\"[psprecomp] loader start\\n\");\n"
                   "  constexpr std::size_t embedded_image_size = "
                << image.memory_size()
                << "U;\n"
                   "  constexpr std::size_t embedded_relocation_size = "
                << image.relocations.size() * 8U
                << "U;\n"
                   "  auto* main_stack = static_cast<std::uint8_t*>("
                   "memalign(64, main_stack_size));\n"
                   "  if (main_stack == nullptr || embedded_image_size != "
                   "psprecomp::generated::guest_memory_size || "
                   "embedded_relocation_size % sizeof("
                   "psprecomp::PspRelocationRecord) != 0) return -1;\n"
                   "  auto* memory = guest_image_start;\n"
                   "  const auto* embedded_relocations = "
                   "guest_relocations_start;\n"
                   "  constexpr auto base = "
                << hex(image.preferred_base)
                << ";\n"
                   "  const auto relocation_result = "
                   "psprecomp::apply_psp_relocations(memory, "
                   "embedded_image_size, base, segments, sizeof(segments) / "
                   "sizeof(segments[0]), reinterpret_cast<const "
                   "psprecomp::PspRelocationRecord*>(embedded_relocations), "
                   "embedded_relocation_size / "
                   "sizeof(psprecomp::PspRelocationRecord));\n"
                   "  if (relocation_result != "
                   "psprecomp::RelocationResult::success) return -2;\n"
                   "  sceKernelPrintf(\"[psprecomp] relocated at %08x\\n\", "
                   "static_cast<unsigned int>(base));\n"
                   "  psprecomp::State state;\n"
                   "  state.memory = memory; state.memory_size = "
                   "embedded_image_size;\n"
                   "  state.memory_base = base; state.direct_memory_access = "
                   "true;\n"
                   "  state.pc = base + psprecomp::generated::guest_entry;\n"
                   "  state.gpr[4] = argc > 0 && argv != nullptr && argv[0] "
                   "!= nullptr ? static_cast<std::uint32_t>("
                   "std::strlen(argv[0]) + 1U) : 0U;\n"
                   "  state.gpr[5] = argc > 0 && argv != nullptr ? "
                   "static_cast<std::uint32_t>(reinterpret_cast<"
                   "std::uintptr_t>(argv[0])) : 0U;\n"
                   "  state.gpr[29] = static_cast<std::uint32_t>("
                   "reinterpret_cast<std::uintptr_t>(main_stack + "
                   "main_stack_size - 64U));\n"
                   "  state.gpr[31] = return_address;\n"
                   "  psprecomp::platform::configure_runtime(memory, "
                   "embedded_image_size, base);\n"
                   "  state.gpr[28] = "
                   "psprecomp::platform::guest_gp_value();\n"
                   "  if (!psprecomp::platform::patch_imports(state)) "
                   "return -3;\n"
                   "  sceKernelPrintf(\"[psprecomp] entering "
                   "module_start\\n\");\n"
                   "  psprecomp::generated::run(state, return_address, "
                   "0x7fffffffffffffffULL);\n"
                   "  sceKernelPrintf(\"[psprecomp] module_start stopped "
                   "stop=%u pc=%08x gp=%08x fault=%08x\\n\", "
                   "static_cast<unsigned int>(state.stop_reason), "
                   "static_cast<unsigned int>(state.pc - base), "
                   "static_cast<unsigned int>(state.gpr[28]), "
                   "static_cast<unsigned int>(state.fault_address));\n"
                   "  sceKernelSleepThread();\n"
                   "  return static_cast<int>(state.gpr[2]);\n"
                   "}\n";
        } else {
            stream << "// Generated by psprecomp. Do not edit.\n"
                  "#include \"generated.hpp\"\n"
                  "#include <psprecomp/relocation.hpp>\n"
                  "#include <pspkernel.h>\n"
                  "#include <cstddef>\n"
                  "#include <cstdint>\n"
                  "#include <cstring>\n\n"
                  "extern \"C\" unsigned char native_guest_image_start[];\n"
                  "extern char __lib_ent_top[], __lib_ent_bottom[];\n"
                  "extern char __lib_stub_top[], __lib_stub_bottom[];\n"
                  "extern SceModuleInfo module_info __attribute__((section("
                  "\".rodata.sceModuleInfo\"), aligned(16), unused)) = {\n"
                  "  0, {0, 1}, \""
               << cpp_string(options.module_name)
               << "\", 0, native_guest_image_start + "
               << hex(image.gp_value_offset)
               << ", __lib_ent_top, __lib_ent_bottom, __lib_stub_top, "
                  "__lib_stub_bottom\n};\n"
                  "PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);\n"
                  "PSP_MAIN_THREAD_STACK_SIZE_KB(256);\n"
                  "// The native bridge performs no dynamic allocation; "
                  "reserve game RAM.\n"
                  "PSP_HEAP_SIZE_KB(0);\n"
                  "PSP_DISABLE_NEWLIB()\n\n"
                  "PSP_NO_CREATE_MAIN_THREAD();\n\n"
                  "extern \"C\" int psprecomp_call_guest(std::uint32_t, "
                  "std::uint32_t, std::uint32_t, void*);\n";
        if (!psp_overlays.empty()) {
            stream << "extern \"C\" bool psprecomp_install_overlays("
                      "std::uint8_t*, std::uint32_t);\n";
        }
        for (const auto& import : platform_imports) {
            stream << "extern \"C\" std::uint32_t " << import.psp_symbol
                   << "(...);\n";
        }
        stream << "\nnamespace {\n"
                  "std::uint32_t read32(const std::uint8_t* bytes) {\n"
                  "  return static_cast<std::uint32_t>(bytes[0]) | "
                  "(static_cast<std::uint32_t>(bytes[1]) << 8U) | "
                  "(static_cast<std::uint32_t>(bytes[2]) << 16U) | "
                  "(static_cast<std::uint32_t>(bytes[3]) << 24U);\n"
                  "}\n"
                  "bool patch_guest_imports(std::uint8_t* memory, "
                  "std::uint32_t base) {\n";
        for (const auto& import : platform_imports) {
            stream << "  { const auto target = static_cast<std::uint32_t>("
                      "reinterpret_cast<std::uintptr_t>(&"
                   << import.psp_symbol
                   << ")); const auto stub = base + "
                   << hex(import.source->stub_address)
                   << "; if (((stub + 4U) & 0xf0000000U) != "
                      "(target & 0xf0000000U)) return false; "
                      "auto* words = reinterpret_cast<volatile "
                      "std::uint32_t*>(memory + "
                   << hex(import.source->stub_address)
                   << "); words[0] = 0x08000000U | "
                      "((target >> 2U) & 0x03ffffffU); words[1] = 0U; }\n";
        }
        stream << "  return true;\n}\n"
                  "constexpr psprecomp::PspLoadSegment segments[] = {\n";
        for (const auto& segment : image.load_segments) {
            stream << "  {" << hex(segment.address) << ", "
                   << hex(segment.memory_size) << ", "
                   << static_cast<unsigned>(segment.program_index) << "},\n";
        }
        stream
            << "};\n"
               "} // namespace\n\n"
               "int main(int argc, char** argv) {\n"
               "  sceKernelPrintf(\"[psprecomp] loader start\\n\");\n"
               "  constexpr std::size_t embedded_image_size = "
            << image.memory_size()
            << "U;\n"
               "  constexpr std::size_t embedded_relocation_size = "
            << native_relocation_bytes.size()
            << "U;\n"
               "  if (embedded_image_size != "
               "psprecomp::generated::guest_memory_size) return -1;\n"
               "  auto* memory = native_guest_image_start;\n"
               "  const auto* embedded_relocations = "
               "memory + "
            << native_relocation_offset
            << "U;\n"
               "  const auto embedded_base = static_cast<std::uint32_t>("
               "reinterpret_cast<std::uintptr_t>(memory));\n"
               "  const auto base = "
            << (image.preferred_base == 0U ? "embedded_base"
                                           : hex(image.preferred_base))
            << ";\n"
               "  const auto relocation_result = "
               "psprecomp::apply_compact_psp_relocations("
               "memory, embedded_image_size, base, segments, "
               "sizeof(segments) / sizeof(segments[0]), "
               "embedded_relocations, embedded_relocation_size, "
            << image.relocations.size()
            << "U);\n"
               "  if (relocation_result != "
               "psprecomp::RelocationResult::success) return -2;\n"
               "  if ("
            << native_relocation_offset
            << "U < embedded_image_size) std::memset(memory + "
            << native_relocation_offset
            << "U, 0, embedded_relocation_size);\n"
               "  if (!patch_guest_imports(memory, base)) return -3;\n";
        if (!psp_overlays.empty()) {
            stream << "  if (!psprecomp_install_overlays(memory, base)) "
                      "return -4;\n";
        }
        stream << "  sceKernelDcacheWritebackInvalidateAll();\n"
               "  sceKernelIcacheInvalidateAll();\n"
               "  sceKernelPrintf(\"[psprecomp] relocated at %08x\\n\", "
               "static_cast<unsigned int>(base));\n"
               "  const auto gp = psprecomp::generated::"
               "guest_gp_pointer_offset != 0xffffffffU ? read32(memory + "
               "psprecomp::generated::guest_gp_pointer_offset) : 0U;\n"
               "  const auto argument_size = argc > 0 && argv != nullptr && "
               "argv[0] != nullptr ? static_cast<std::uint32_t>("
               "std::strlen(argv[0]) + 1U) : 0U;\n"
               "  void* argument = argc > 0 && argv != nullptr ? argv[0] : "
               "nullptr;\n"
               "  sceKernelPrintf(\"[psprecomp] entering native "
               "module_start\\n\");\n"
               "  const auto result = psprecomp_call_guest(base + "
               "psprecomp::generated::guest_entry, gp, argument_size, "
               "argument);\n"
               "  sceKernelPrintf(\"[psprecomp] module_start returned "
               "%08x\\n\", static_cast<unsigned int>(result));\n"
               "  sceKernelSleepThread();\n"
               "  return result;\n"
               "}\n";
        }
    }
    {
        std::ofstream stream(platform_directory / "macos" / "platform.cpp",
                             std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("cannot create generated macOS platform");
        }
        auto image_start = image.memory_size();
        for (const auto& segment : image.load_segments) {
            image_start = std::min(image_start,
                                   static_cast<std::uint32_t>(segment.address));
        }
        stream
            << "// Generated by psprecomp. Extend these host implementations "
               "as the port grows.\n"
               "#include \"generated.hpp\"\n"
               "#include <platform/platform.h>\n"
               "#include <refract/psp_sdk_stubs.hpp>\n"
               "#include <refract/refract.hpp>\n"
               "#include <utility>\n\n"
               "namespace psprecomp::platform {\n"
               "namespace {\n"
               "std::uint32_t shared_memory_gp;\n"
               "} // namespace\n\n"
               "void configure_runtime(std::uint8_t* memory, "
               "std::size_t size, std::uint32_t base) {\n"
               "  shared_memory_gp = generated::guest_gp_pointer_offset "
               "!= 0xffffffffU && generated::guest_gp_pointer_offset <= "
               "size - 4U ? (static_cast<std::uint32_t>("
               "memory[generated::guest_gp_pointer_offset]) | "
               "(static_cast<std::uint32_t>(memory["
               "generated::guest_gp_pointer_offset + 1U]) << 8U) | "
               "(static_cast<std::uint32_t>(memory["
               "generated::guest_gp_pointer_offset + 2U]) << 16U) | "
               "(static_cast<std::uint32_t>(memory["
               "generated::guest_gp_pointer_offset + 3U]) << 24U)) : 0U;\n"
               "  refract::Configuration configuration;\n"
               "  configuration.image_size = generated::guest_memory_size + "
               "0x1000U;\n"
               "  configuration.image_start = "
            << image_start
            << "U;\n"
               "  configuration.guest_executor = [](State& state) {\n"
               "    generated::run(state, 0xfffffff0U, "
               "0x7fffffffffffffffULL);\n"
               "  };\n"
               "  refract::Runtime::instance().configure(memory, size, base, "
               "std::move(configuration));\n"
               "}\n\n"
               "std::uint32_t guest_gp_value() {\n"
               "  return shared_memory_gp;\n"
               "}\n\n"
               "void log(const char* format, std::uint32_t first, "
               "std::uint32_t second) {\n"
               "  refract::Runtime::instance().log(format, first, "
               "second);\n"
               "}\n\n"
               "bool patch_imports(State& state) {\n"
               "  refract::Runtime::instance().prepare_state(state);\n"
               "  return true;\n"
               "}\n\n"
               "bool dispatch_import(State& state, "
               "std::uint32_t current_pc) {\n"
               "  switch (current_pc - state.memory_base) {\n";
        for (const auto& import : platform_imports) {
            stream << "  case " << hex(import.source->stub_address) << ": ";
            if (!import.symbol.empty() && is_psp_sdk_stub(import.symbol)) {
                stream << "refract::pspsdk::" << import.symbol
                       << "(state);";
            } else {
                stream << "state.gpr[2] = 0x8002013aU; "
                          "log(\"[psprecomp] unimplemented import "
                          "nid=%08x stub=%08x\\n\", "
                       << hex(import.source->nid)
                       << ", current_pc - state.memory_base);";
            }
            stream << " state.pc = state.gpr[31]; return true;\n";
        }
        stream << "  default: return false;\n"
                  "  }\n"
                  "}\n"
                  "} // namespace psprecomp::platform\n";
    }
    {
        std::ofstream stream(platform_directory / "macos" / "main.cpp",
                             std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error(
                "cannot create generated macOS entry point");
        }
        stream << "// Generated by psprecomp. Native debug entry point.\n"
                  "#include \"generated.hpp\"\n"
                  "#include <platform/platform.h>\n"
                  "#include <psprecomp/relocation.hpp>\n"
                  "#include <refract/refract.hpp>\n"
                  "#include <cstddef>\n"
                  "#include <cstdint>\n"
                  "#include <cstdlib>\n"
                  "#include <cstdio>\n"
                  "#include <cstring>\n"
                  "#include <filesystem>\n"
                  "#include <fstream>\n"
                  "#include <vector>\n\n"
                  "namespace {\n"
                  "constexpr psprecomp::PspLoadSegment segments[] = {\n";
        for (const auto& segment : image.load_segments) {
            stream << "  {" << hex(segment.address) << ", "
                   << hex(segment.memory_size) << ", "
                   << static_cast<unsigned>(segment.program_index) << "},\n";
        }
        stream
            << "};\n"
               "constexpr std::uint32_t guest_base = "
            << hex(image.preferred_base == 0 ? 0x08800000U
                                             : image.preferred_base)
            << ";\n"
               "constexpr std::uint32_t return_address = 0xfffffff0U;\n"
               "std::vector<std::uint8_t> read_file("
               "const std::filesystem::path& path) {\n"
               "  std::ifstream input(path, std::ios::binary | "
               "std::ios::ate);\n"
               "  if (!input) return {};\n"
               "  const auto size = input.tellg();\n"
               "  if (size < 0) return {};\n"
               "  std::vector<std::uint8_t> bytes("
               "static_cast<std::size_t>(size));\n"
               "  input.seekg(0);\n"
               "  if (!bytes.empty()) input.read("
               "reinterpret_cast<char*>(bytes.data()), size);\n"
               "  return input ? bytes : std::vector<std::uint8_t>{};\n"
               "}\n"
               "} // namespace\n\n"
               "int main(int argc, char** argv) {\n"
               "  (void)argc;\n"
               "  refract::Runtime::instance().set_verbose(true);\n"
               "  const auto executable = "
               "std::filesystem::weakly_canonical(argv[0]);\n"
               "  const auto resources = "
               "executable.parent_path().parent_path() "
               "/ \"Resources\";\n"
               "  auto project_root = resources;\n"
               "  while (!std::filesystem::is_directory(project_root / "
               "\"disc\") && project_root != project_root.parent_path())\n"
               "    project_root = project_root.parent_path();\n"
               "  const auto found_project_root = "
               "std::filesystem::is_directory(project_root / \"disc\");\n"
               "  if (std::getenv(\"REFRACT_DISC_ROOT\") == nullptr && "
               "found_project_root)\n"
               "    setenv(\"REFRACT_DISC_ROOT\", "
               "(project_root / \"disc\").c_str(), 0);\n"
               "  if (found_project_root && "
               "std::getenv(\"REFRACT_DISC_IMAGE\") == nullptr && "
               "std::filesystem::is_regular_file(project_root / "
               "\"original\" / \"disc.iso\"))\n"
               "    setenv(\"REFRACT_DISC_IMAGE\", (project_root / "
               "\"original\" / \"disc.iso\").c_str(), 0);\n"
               "  if (found_project_root && "
               "std::getenv(\"REFRACT_WRITABLE_ROOT\") == nullptr)\n"
               "    setenv(\"REFRACT_WRITABLE_ROOT\", (project_root / "
               "\".refract\" / \"ms0\").c_str(), 0);\n"
               "  auto memory = read_file(resources / \"guest_image.bin\");\n"
               "  const auto relocation_bytes = "
               "read_file(resources / \"relocations.bin\");\n"
               "  if (memory.size() != "
            << image.memory_size()
            << "U || relocation_bytes.size() % "
               "sizeof(psprecomp::PspRelocationRecord) != 0) {\n"
               "    std::fprintf(stderr, \"psprecomp: missing or invalid app "
               "resources\\n\"); return 1;\n"
               "  }\n"
               "  const auto image_size = memory.size();\n"
               "  constexpr std::uint32_t guest_ram_end = 0x0a000000U;\n"
               "  if (guest_base >= guest_ram_end || image_size > "
               "guest_ram_end - guest_base) return 3;\n"
               "  memory.resize(static_cast<std::size_t>(guest_ram_end - "
               "guest_base));\n"
               "  const auto relocation_result = "
               "psprecomp::apply_psp_relocations(\n"
               "      memory.data(), image_size, guest_base, segments, "
               "sizeof(segments) / sizeof(segments[0]),\n"
               "      reinterpret_cast<const psprecomp::PspRelocationRecord*>("
               "relocation_bytes.data()), relocation_bytes.size() / "
               "sizeof(psprecomp::PspRelocationRecord));\n"
               "  if (relocation_result != "
               "psprecomp::RelocationResult::success) return 2;\n"
               "  psprecomp::State state;\n"
               "  state.memory = memory.data(); state.memory_size = "
               "memory.size();\n"
               "  state.memory_base = guest_base; "
               "state.direct_memory_access = false;\n"
               "  state.pc = guest_base + psprecomp::generated::guest_entry;\n"
               "  const auto argument_size = std::strlen(argv[0]) + 1U;\n"
               "  const auto argument_offset = (image_size + 15U) & ~15U;\n"
               "  if (argument_offset + argument_size > memory.size()) "
               "return 3;\n"
               "  std::memcpy(memory.data() + argument_offset, argv[0], "
               "argument_size);\n"
               "  state.gpr[4] = static_cast<std::uint32_t>(argument_size);\n"
               "  state.gpr[5] = guest_base + "
               "static_cast<std::uint32_t>(argument_offset);\n"
               "  state.gpr[29] = guest_base + "
               "static_cast<std::uint32_t>(memory.size()) - 64U;\n"
               "  state.gpr[31] = return_address;\n"
               "  psprecomp::platform::configure_runtime(memory.data(), "
               "memory.size(), guest_base);\n"
               "  state.gpr[28] = psprecomp::platform::guest_gp_value();\n"
               "  if (!psprecomp::platform::patch_imports(state)) return 3;\n"
               "  std::fprintf(stderr, "
               "\"[psprecomp:macos] entering module_start\\n\");\n"
               "  refract::Runtime::instance().execute_guest(state);\n"
               "  std::fprintf(stderr, "
               "\"[psprecomp:macos] stopped: "
               "reason=%u pc=%08x\\n\", static_cast<unsigned>("
               "state.stop_reason), static_cast<unsigned>(state.pc));\n"
               "  refract::Runtime::instance().run_host_loop();\n"
               "  return static_cast<int>(state.gpr[2]);\n"
               "}\n";
    }
    {
        std::ofstream stream(directory / "psp_entry.S",
                             std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("cannot create PSP guest trampoline");
        }
        stream << "# Calls relocated Allegrex code while preserving the "
                  "generated loader's $gp.\n"
                  ".set noreorder\n.text\n.align 2\n"
                  ".globl psprecomp_call_guest\n"
                  ".type psprecomp_call_guest, @function\n"
                  ".ent psprecomp_call_guest, 0\n"
                  "psprecomp_call_guest:\n"
                  "addiu $sp, $sp, -32\n"
                  "sw $ra, 28($sp)\n"
                  "sw $gp, 24($sp)\n"
                  "move $t9, $a0\n"
                  "move $gp, $a1\n"
                  "move $a0, $a2\n"
                  "move $a1, $a3\n"
                  "jalr $t9\n"
                  "nop\n"
                  "lw $gp, 24($sp)\n"
                  "lw $ra, 28($sp)\n"
                  "jr $ra\n"
                  "addiu $sp, $sp, 32\n"
                  ".end psprecomp_call_guest\n"
                  ".size psprecomp_call_guest, .-psprecomp_call_guest\n";
    }
    if (!psp_overlays.empty()) {
        {
            std::ofstream stream(platform_directory / "psp" / "overlay.cpp",
                                 std::ios::binary | std::ios::trunc);
            if (!stream) {
                throw std::runtime_error(
                    "cannot create generated PSP overlay runtime");
            }
            stream << "// Generated by psprecomp. Do not edit.\n"
                      "#include \"generated.hpp\"\n"
                      "#include <psprecomp/overlay.hpp>\n"
                      "#include <cstddef>\n"
                      "#include <cstdint>\n\n"
                      "namespace psprecomp::generated {\n";
            for (const auto& overlay : psp_overlays) {
                stream << "bool " << function_name(overlay.start)
                       << "(State&, std::uint32_t);\n";
            }
            stream << "} // namespace psprecomp::generated\n\n";
            for (const auto& route : psp_overlay_routes) {
                stream << "extern \"C\" void psprecomp_overlay_stub_"
                       << route.index << "();\n";
            }
            stream << "\nnamespace {\n"
                      "using Runner = bool (*)(psprecomp::State&, "
                      "std::uint32_t);\n"
                      "struct Overlay { std::uint32_t start; "
                      "std::uint32_t end; Runner runner; "
                      "std::size_t entry_route; };\n"
                      "struct Route { std::size_t overlay_index; "
                      "std::uint32_t entry; std::uint32_t link_register; "
                      "bool resume; void (*stub)(); };\n"
                      "constexpr Overlay overlays[] = {\n";
            for (const auto& overlay : psp_overlays) {
                stream << "  {" << hex(overlay.start) << ", "
                       << hex(overlay.end)
                       << ", &psprecomp::generated::"
                       << function_name(overlay.start)
                       << ", " << overlay.entry_route
                       << "},\n";
            }
            stream << "};\n"
                      "constexpr Route routes[] = {\n";
            for (const auto& route : psp_overlay_routes) {
                stream << "  {" << route.overlay_index << ", "
                       << hex(route.entry) << ", " << route.link_register
                       << ", "
                       << (route.resume ? "true" : "false")
                       << ", &psprecomp_overlay_stub_" << route.index
                       << "},\n";
            }
            stream << "};\n"
                      "std::uint8_t* guest_memory;\n"
                      "std::uint32_t guest_base;\n"
                      "bool overlays_ready;\n"
                      "\n"
                      "void copy_to_state(const psprecomp::PspOverlayContext& "
                      "context, psprecomp::State& state) {\n"
                      "  for (std::size_t i = 0; i < 32U; ++i) "
                      "state.gpr[i] = context.gpr[i];\n"
                      "  state.gpr[0] = 0U; state.hi = context.hi; "
                      "state.lo = context.lo;\n"
                      "  for (std::size_t i = 0; i < 32U; ++i) "
                      "state.fpr[i] = context.fpr[i];\n"
                      "  state.fcr31 = context.fcr31;\n"
                      "  for (std::size_t matrix = 0; matrix < 8U; "
                      "++matrix) {\n"
                      "    for (std::size_t column = 0; column < 4U; "
                      "++column) {\n"
                      "      for (std::size_t row = 0; row < 4U; ++row) {\n"
                      "        const auto physical = (matrix * 4U + column) "
                      "* 4U + row;\n"
                      "        const auto logical = matrix * 4U + column + "
                      "row * 32U;\n"
                      "        state.vfpu[logical] = context.vfpu[physical];\n"
                      "      }\n"
                      "    }\n"
                      "  }\n"
                      "  for (std::size_t i = 0; i < 16U; ++i) "
                      "state.vfpu_ctrl[i] = context.vfpu_ctrl[i];\n"
                      "}\n"
                      "\n"
                      "void copy_from_state(const psprecomp::State& state, "
                      "psprecomp::PspOverlayContext& context) {\n"
                      "  for (std::size_t i = 0; i < 32U; ++i) "
                      "context.gpr[i] = state.gpr[i];\n"
                      "  context.gpr[0] = 0U; context.hi = state.hi; "
                      "context.lo = state.lo;\n"
                      "  for (std::size_t i = 0; i < 32U; ++i) "
                      "context.fpr[i] = state.fpr[i];\n"
                      "  context.fcr31 = state.fcr31;\n"
                      "  for (std::size_t matrix = 0; matrix < 8U; "
                      "++matrix) {\n"
                      "    for (std::size_t column = 0; column < 4U; "
                      "++column) {\n"
                      "      for (std::size_t row = 0; row < 4U; ++row) {\n"
                      "        const auto physical = (matrix * 4U + column) "
                      "* 4U + row;\n"
                      "        const auto logical = matrix * 4U + column + "
                      "row * 32U;\n"
                      "        context.vfpu[physical] = state.vfpu[logical];\n"
                      "      }\n"
                      "    }\n"
                      "  }\n"
                      "  for (std::size_t i = 0; i < 16U; ++i) "
                      "context.vfpu_ctrl[i] = state.vfpu_ctrl[i];\n"
                      "  context.pc = state.pc;\n"
                      "}\n"
                      "} // namespace\n\n"
                      "extern \"C\" bool psprecomp_install_overlays("
                      "std::uint8_t* memory, std::uint32_t base) {\n"
                      "  if (memory == nullptr) return false;\n"
                      "  guest_memory = memory; guest_base = base;\n"
                      "  for (const auto& overlay : overlays) {\n"
                      "    if (overlay.start > "
                      "psprecomp::generated::guest_memory_size || "
                      "psprecomp::generated::guest_memory_size - "
                      "overlay.start < 8U) return false;\n"
                      "    const auto entry = base + overlay.start;\n"
                      "    const auto& route = routes[overlay.entry_route];\n"
                      "    const auto target = static_cast<std::uint32_t>("
                      "reinterpret_cast<std::uintptr_t>(route.stub));\n"
                      "    if (((entry + 4U) & 0xf0000000U) != "
                      "(target & 0xf0000000U)) return false;\n"
                      "    auto* words = reinterpret_cast<volatile "
                      "std::uint32_t*>(memory + overlay.start);\n"
                      "    words[0] = 0x08000000U | ((target >> 2U) & "
                      "0x03ffffffU); words[1] = 0U;\n"
                      "  }\n"
                      "  overlays_ready = true; return true;\n"
                      "}\n\n"
                      "extern \"C\" void psprecomp_overlay_run("
                      "std::uint32_t route_index, "
                      "psprecomp::PspOverlayContext* context) {\n"
                      "  if (context == nullptr || !overlays_ready || "
                      "route_index >= sizeof(routes) / sizeof(routes[0])) "
                      "return;\n"
                      "  const auto& route = routes[route_index];\n"
                      "  const auto& overlay = overlays[route.overlay_index];\n"
                      "  psprecomp::State state; copy_to_state(*context, "
                      "state);\n"
                      "  state.memory = guest_memory; state.memory_size = "
                      "psprecomp::generated::guest_memory_size;\n"
                      "  state.memory_base = guest_base; "
                      "state.direct_memory_access = true;\n"
                      "  state.pc = guest_base + route.entry;\n"
                      "  if (route.resume) "
                      "state.gpr[route.link_register] = state.pc;\n"
                      "  state.stop_reason = psprecomp::StopReason::running;\n"
                      "  const bool handled = overlay.runner(state, state.pc);\n"
                      "  const auto overlay_begin = guest_base + "
                      "overlay.start;\n"
                      "  const auto overlay_end = guest_base + overlay.end;\n"
                      "  const bool pc_in_overlay = state.pc >= "
                      "overlay_begin && state.pc < overlay_end;\n"
                      "  bool outbound_call = false;\n"
                      "  if (!pc_in_overlay || state.pc == overlay_begin) {\n"
                      "    for (const auto& candidate : routes) {\n"
                      "      if (!candidate.resume || "
                      "candidate.overlay_index != route.overlay_index || "
                      "state.gpr[candidate.link_register] != "
                      "guest_base + candidate.entry) "
                      "continue;\n"
                      "      state.gpr[candidate.link_register] = "
                      "static_cast<std::uint32_t>("
                      "reinterpret_cast<std::uintptr_t>(candidate.stub));\n"
                      "      outbound_call = true; break;\n"
                      "    }\n"
                      "  }\n"
                      "  if (!handled || state.stop_reason != "
                      "psprecomp::StopReason::running || "
                      "(pc_in_overlay && !(outbound_call && "
                      "state.pc == overlay_begin))) {\n"
                      "    copy_from_state(state, *context);\n"
                      "    context->gpr[2] = 0x80020001U; "
                      "context->pc = state.gpr[31]; return;\n"
                      "  }\n"
                      "  copy_from_state(state, *context);\n"
                      "}\n";
        }
        {
            std::ofstream stream(directory / "psp_overlays.S",
                                 std::ios::binary | std::ios::trunc);
            if (!stream) {
                throw std::runtime_error(
                    "cannot create generated PSP overlay trampoline");
            }
            stream << "# Generated full Allegrex/FPU/VFPU overlay bridge.\n"
                      ".set noreorder\n.set noat\n.text\n.align 2\n";
            for (const auto& route : psp_overlay_routes) {
                stream << ".globl psprecomp_overlay_stub_" << route.index
                       << "\n.type psprecomp_overlay_stub_" << route.index
                       << ", @function\npsprecomp_overlay_stub_"
                       << route.index << ":\n";
                stream << "addiu $k0, $zero, "
                       << (route.uses_vfpu ? 1 : 0) << "\n";
                if (route.index <= 0x7fffU) {
                    stream << "addiu $k1, $zero, " << route.index << "\n";
                } else {
                    stream << "lui $k1, " << (route.index >> 16U) << "\n"
                           << "ori $k1, $k1, "
                           << (route.index & 0xffffU) << "\n";
                }
                stream << "b psprecomp_overlay_common\nnop\n"
                       << ".size psprecomp_overlay_stub_" << route.index
                       << ", .-psprecomp_overlay_stub_" << route.index
                       << "\n";
            }
            stream << ".align 4\n.type psprecomp_overlay_common, @function\n"
                      ".ent psprecomp_overlay_common, 0\n"
                      "psprecomp_overlay_common:\n"
                      "addiu $sp, $sp, -864\n"
                      "sw $zero, 0($sp)\n"
                      "sw $at, 4($sp)\n"
                      "sw $v0, 8($sp)\n"
                      "sw $v1, 12($sp)\n"
                      "sw $a0, 16($sp)\n"
                      "sw $a1, 20($sp)\n"
                      "sw $a2, 24($sp)\n"
                      "sw $a3, 28($sp)\n"
                      "sw $t0, 32($sp)\n"
                      "sw $t1, 36($sp)\n"
                      "sw $t2, 40($sp)\n"
                      "sw $t3, 44($sp)\n"
                      "sw $t4, 48($sp)\n"
                      "sw $t5, 52($sp)\n"
                      "sw $t6, 56($sp)\n"
                      "sw $t7, 60($sp)\n"
                      "sw $s0, 64($sp)\n"
                      "sw $s1, 68($sp)\n"
                      "sw $s2, 72($sp)\n"
                      "sw $s3, 76($sp)\n"
                      "sw $s4, 80($sp)\n"
                      "sw $s5, 84($sp)\n"
                      "sw $s6, 88($sp)\n"
                      "sw $s7, 92($sp)\n"
                      "sw $t8, 96($sp)\n"
                      "sw $t9, 100($sp)\n"
                      "sw $k0, 104($sp)\n"
                      "sw $k1, 108($sp)\n"
                      "sw $k0, 268($sp)\n"
                      "sw $gp, 112($sp)\n"
                      "addiu $k0, $sp, 864\n"
                      "sw $k0, 116($sp)\n"
                      "sw $fp, 120($sp)\n"
                      "sw $ra, 124($sp)\n"
                      "mfhi $k0\nsw $k0, 128($sp)\n"
                      "mflo $k0\nsw $k0, 132($sp)\n";
            for (std::size_t i = 0; i < 32U; ++i) {
                stream << "swc1 $f" << i << ", " << (136U + i * 4U)
                       << "($sp)\n";
            }
            stream << "cfc1 $k1, $31\nsw $k1, 264($sp)\n"
                      "lw $k0, 268($sp)\n"
                      "beq $k0, $zero, psprecomp_overlay_vfpu_saved\n"
                      "nop\n";
            for (std::size_t matrix = 0; matrix < 8U; ++matrix) {
                for (std::size_t column = 0; column < 4U; ++column) {
                    const auto quad = matrix * 4U + column;
                    stream << "sv.q C" << matrix << column << "0, "
                           << (272U + quad * 16U) << "($sp)\n";
                }
            }
            for (std::size_t i = 0; i < 16U; ++i) {
                stream << "mfvc $k0, $" << (128U + i) << "\n"
                       << "sw $k0, " << (784U + i * 4U) << "($sp)\n";
            }
            stream << "psprecomp_overlay_vfpu_saved:\n"
                      "move $a1, $sp\n"
                      "jal psprecomp_overlay_run\n"
                      "lw $a0, 108($sp)\n"
                      "lw $k0, 268($sp)\n"
                      "beq $k0, $zero, psprecomp_overlay_vfpu_restored\n"
                      "nop\n";
            for (std::size_t matrix = 0; matrix < 8U; ++matrix) {
                for (std::size_t column = 0; column < 4U; ++column) {
                    const auto quad = matrix * 4U + column;
                    stream << "lv.q C" << matrix << column << "0, "
                           << (272U + quad * 16U) << "($sp)\n";
                }
            }
            for (std::size_t i = 0; i < 16U; ++i) {
                stream << "lw $k0, " << (784U + i * 4U) << "($sp)\n"
                       << "mtvc $k0, $" << (128U + i) << "\n";
            }
            stream << "psprecomp_overlay_vfpu_restored:\n";
            for (std::size_t i = 0; i < 32U; ++i) {
                stream << "lwc1 $f" << i << ", " << (136U + i * 4U)
                       << "($sp)\n";
            }
            stream << "lw $k0, 264($sp)\nctc1 $k0, $31\n"
                      "lw $k0, 128($sp)\nmthi $k0\n"
                      "lw $k0, 132($sp)\nmtlo $k0\n"
                      "lw $k1, 848($sp)\n"
                      "lw $at, 4($sp)\n"
                      "lw $v0, 8($sp)\n"
                      "lw $v1, 12($sp)\n"
                      "lw $a0, 16($sp)\n"
                      "lw $a1, 20($sp)\n"
                      "lw $a2, 24($sp)\n"
                      "lw $a3, 28($sp)\n"
                      "lw $t0, 32($sp)\n"
                      "lw $t1, 36($sp)\n"
                      "lw $t2, 40($sp)\n"
                      "lw $t3, 44($sp)\n"
                      "lw $t4, 48($sp)\n"
                      "lw $t5, 52($sp)\n"
                      "lw $t6, 56($sp)\n"
                      "lw $t7, 60($sp)\n"
                      "lw $s0, 64($sp)\n"
                      "lw $s1, 68($sp)\n"
                      "lw $s2, 72($sp)\n"
                      "lw $s3, 76($sp)\n"
                      "lw $s4, 80($sp)\n"
                      "lw $s5, 84($sp)\n"
                      "lw $s6, 88($sp)\n"
                      "lw $s7, 92($sp)\n"
                      "lw $t8, 96($sp)\n"
                      "lw $t9, 100($sp)\n"
                      "lw $gp, 112($sp)\n"
                      "lw $fp, 120($sp)\n"
                      "lw $ra, 124($sp)\n"
                      "lw $k0, 104($sp)\n"
                      "lw $sp, 116($sp)\n"
                      "jr $k1\nnop\n"
                      ".end psprecomp_overlay_common\n"
                      ".size psprecomp_overlay_common, "
                      ".-psprecomp_overlay_common\n";
        }
    }
    {
        std::map<std::pair<std::string, std::uint32_t>,
                 std::vector<const PlatformImport*>> import_libraries;
        for (const auto& import : platform_imports) {
            import_libraries[{import.source->library,
                              import.source->library_flags}]
                .push_back(&import);
        }
        std::ofstream stream(directory / "imports.S",
                             std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("cannot create generated PSP imports");
        }
        stream << "# Generated from the original PRX import metadata.\n"
                  ".set noreorder\n";
        std::size_t library_index = 0;
        for (const auto& [key, imports] : import_libraries) {
            if (imports.size() > std::numeric_limits<std::uint16_t>::max()) {
                throw std::runtime_error("PSP import library has too many stubs");
            }
            const auto label = "psprecomp_library_" +
                               std::to_string(library_index++);
            stream << ".section .rodata.sceResident, \"a\"\n"
                      ".word 0\n."
                   << label << "_name:\n.asciz \""
                   << cpp_string(key.first)
                   << "\"\n.align 2\n"
                      ".section .lib.stub, \"a\", @progbits\n.word ."
                   << label << "_name\n.word " << hex_address(key.second)
                   << "\n.word "
                   << hex_address((static_cast<std::uint32_t>(imports.size())
                                   << 16U) |
                                  5U)
                   << "\n.word ." << label << "_nids\n.word ." << label
                   << "_stubs\n"
                      ".section .rodata.sceNid, \"a\"\n."
                   << label << "_nids:\n";
            for (const auto* import : imports) {
                stream << ".word " << hex_address(import->source->nid) << "\n";
            }
            stream << ".section .sceStub.text, \"ax\", @progbits\n."
                   << label << "_stubs:\n";
            for (const auto* import : imports) {
                stream << ".globl " << import->psp_symbol << "\n.type "
                       << import->psp_symbol << ", @function\n.ent "
                       << import->psp_symbol << ", 0\n"
                       << import->psp_symbol << ":\n"
                          "jr $ra\nnop\n.end "
                       << import->psp_symbol << "\n.size "
                       << import->psp_symbol << ", .-" << import->psp_symbol
                       << "\n";
            }
        }
    }
    {
        std::ofstream stream(directory / "relocations.bin",
                             std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("cannot create relocation image");
        }
        for (const auto& relocation : image.relocations) {
            const std::uint8_t record[8] = {
                static_cast<std::uint8_t>(relocation.offset),
                static_cast<std::uint8_t>(relocation.offset >> 8U),
                static_cast<std::uint8_t>(relocation.offset >> 16U),
                static_cast<std::uint8_t>(relocation.offset >> 24U),
                relocation.type,
                relocation.patch_segment,
                relocation.target_segment,
                0,
            };
            stream.write(reinterpret_cast<const char*>(record), sizeof(record));
        }
    }
    {
        std::ofstream stream(directory / "native_relocations.bin",
                             std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error(
                "cannot create compact native relocation image");
        }
        stream.write(
            reinterpret_cast<const char*>(native_relocation_bytes.data()),
            static_cast<std::streamsize>(native_relocation_bytes.size()));
    }
    {
        std::ofstream stream(directory / "imports.tsv",
                             std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("cannot create import manifest");
        }
        stream << "stub_address\tnid\tlibrary\tsymbol\n";
        for (const auto& import : image.imports) {
            stream << "0x" << std::hex << std::setfill('0') << std::setw(8)
                   << import.stub_address << "\t0x" << std::setw(8)
                   << import.nid << "\t" << import.library << "\t";
            stream << import_symbol(import, code_map);
            stream << "\n";
        }
    }
    {
        std::ofstream stream(directory / "generated_sources.cmake",
                             std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("cannot create generated source manifest");
        }
        stream << "set(PSPRECOMP_GENERATED_SOURCES\n";
        for (const auto& name : source_names) {
            stream << "  ${CMAKE_CURRENT_LIST_DIR}/" << name << "\n";
        }
        stream << ")\n";
    }
    {
        std::ofstream stream(directory / "Makefile",
                             std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("cannot create generated PSP Makefile");
        }
        const auto psp_sources =
            std::filesystem::relative(platform_directory / "psp", directory)
                .generic_string();
        const auto refract_sources =
            std::filesystem::relative(
                platform_directory.parent_path() / "refract" /
                    "src",
                directory)
                .generic_string();
        const auto project_root =
            std::filesystem::relative(platform_directory.parent_path(),
                                      directory)
                .generic_string();
        stream << "PSPSDK := $(shell psp-config --pspsdk-path)\n"
                  "TARGET = "
               << make_value(options.target_name)
               << "\n"
                  "PSPRECOMP_PROJECT_ROOT ?= "
               << make_value(project_root)
               << "\n"
                  "VPATH = "
               << make_value(psp_sources)
               << " "
               << make_value(refract_sources)
               << "\n";
        if (image.preferred_base == 0U) {
            stream << "OBJS = main.o imports.o psp_entry.o";
            if (!psp_overlays.empty()) {
                stream << " overlay.o psp_overlays.o";
                for (const auto& overlay : psp_overlays) {
                    std::ostringstream object;
                    object << " func_" << std::hex << std::setfill('0')
                           << std::setw(8) << overlay.start << ".o";
                    stream << object.str();
                }
            }
            stream << " native_guest_image.o\n";
        } else {
            stream << "OBJS = main.o platform.o dispatch.o "
                      "psp_sdk_stubs.o";
            for (const auto& name : source_names) {
                if (name.rfind("shard_", 0) == 0 ||
                    name.rfind("func_", 0) == 0) {
                    stream << " " << name.substr(0, name.size() - 4) << ".o";
                }
            }
            stream << " guest_image.o guest_relocations.o\n";
        }
        stream << "BUILD_PRX = 1\n"
                  "EXTRA_TARGETS = EBOOT.PBP\n"
                  "PSP_EBOOT_TITLE = "
               << make_value(options.display_name)
               << "\n"
                  "PSPRECOMP_INCLUDE ?= "
               << make_value(options.include_path)
               << "\n"
                  "CFLAGS = -Os -G0 -Wall -I$(PSPRECOMP_INCLUDE) "
                  "-I$(PSPRECOMP_PROJECT_ROOT) "
                  "-I$(PSPRECOMP_PROJECT_ROOT)/refract/include -I.\n"
                  "CXXFLAGS = $(CFLAGS) -std=c++20 -fno-exceptions -fno-rtti\n"
                  "ASFLAGS = $(CFLAGS)\n"
                  "LIBS = -lpsputility -lpspmp3 -lpspdmac -lpspmpeg "
                  "-lpspmpegbase -lpspatrac3 "
                  "-lpspsascore -lpspaudio -lpspctrl -lpspdisplay -lpspge "
                  "-lpsppower -lpspusb -lpspumd -lpspwlan "
                  "-lpspnet_adhocctl -lpspnet_adhocmatching -lpspnet_adhoc "
                  "-lpspnet\n\n";
        for (const auto& overlay : psp_overlays) {
            stream << "func_" << std::hex << std::setfill('0')
                   << std::setw(8) << overlay.start
                   << ".o: CXXFLAGS += -DPSPRECOMP_PSP_OVERLAY\n";
        }
        stream << "\ninclude $(PSPSDK)/lib/build.mak\n\n"
                  "guest_image.o: guest_image.bin\n"
                  "\tbin2o -n -i -a 64 $< $@ guest_image\n\n"
                  "native_guest_image.o: native_guest_image.bin\n"
                  "\tbin2o -n -i -a 64 $< $@ native_guest_image\n\n"
                  "guest_relocations.o: relocations.bin\n"
                  "\tbin2o -n -i -a 4 $< $@ guest_relocations\n";
    }
}

bool analyze_coverage(const ElfImage& image, const CodeMap* code_map,
                      std::ostream& output) {
    struct MissingGroup {
        std::size_t count{};
        std::uint32_t example_pc{};
        std::uint32_t example_instruction{};
    };
    std::map<std::string, MissingGroup> invalid;
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
