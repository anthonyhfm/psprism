#pragma once

#include <cstddef>
#include <cstdint>

namespace psprecomp {

struct PspRelocationRecord {
    std::uint32_t offset{};
    std::uint8_t type{};
    std::uint8_t patch_segment{};
    std::uint8_t target_segment{};
    std::uint8_t reserved{};
};

struct PspLoadSegment {
    std::uint32_t address{};
    std::uint32_t memory_size{};
    std::uint8_t program_index{};
};

enum class RelocationResult : std::uint8_t {
    success,
    invalid_segment,
    invalid_patch,
    missing_lo16,
    unsupported_type,
};

namespace detail {

inline const PspLoadSegment* find_segment(const PspLoadSegment* segments,
                                          std::size_t segment_count,
                                          std::uint8_t program_index) {
    for (std::size_t i = 0; i < segment_count; ++i) {
        if (segments[i].program_index == program_index) {
            return &segments[i];
        }
    }
    return nullptr;
}

inline bool relocation_word_offset(const PspRelocationRecord& relocation,
                                   const PspLoadSegment* segments,
                                   std::size_t segment_count,
                                   std::size_t memory_size,
                                   std::size_t& result) {
    const auto* segment =
        find_segment(segments, segment_count, relocation.patch_segment);
    if (segment == nullptr || relocation.offset > segment->memory_size ||
        segment->memory_size - relocation.offset < 4U) {
        return false;
    }
    result = static_cast<std::size_t>(segment->address) + relocation.offset;
    return result <= memory_size && 4U <= memory_size - result;
}

inline std::uint32_t read_u32(const std::uint8_t* memory, std::size_t offset) {
    return static_cast<std::uint32_t>(memory[offset]) |
           static_cast<std::uint32_t>(memory[offset + 1]) << 8U |
           static_cast<std::uint32_t>(memory[offset + 2]) << 16U |
           static_cast<std::uint32_t>(memory[offset + 3]) << 24U;
}

inline void write_u32(std::uint8_t* memory, std::size_t offset,
                      std::uint32_t value) {
    memory[offset] = static_cast<std::uint8_t>(value);
    memory[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
    memory[offset + 2] = static_cast<std::uint8_t>(value >> 16U);
    memory[offset + 3] = static_cast<std::uint8_t>(value >> 24U);
}

} // namespace detail

// Applies the compact SHT_PSPREL relocation stream used by PSP PRX files.
// guest_base is the 32-bit address represented by memory[0].
inline RelocationResult apply_psp_relocations(
    std::uint8_t* memory, std::size_t memory_size, std::uint32_t guest_base,
    const PspLoadSegment* segments, std::size_t segment_count,
    const PspRelocationRecord* relocations, std::size_t relocation_count) {
    if (memory == nullptr || segments == nullptr ||
        (relocation_count != 0 && relocations == nullptr)) {
        return RelocationResult::invalid_patch;
    }

    for (std::size_t i = 0; i < relocation_count; ++i) {
        const auto& relocation = relocations[i];
        const auto* target = detail::find_segment(
            segments, segment_count, relocation.target_segment);
        if (target == nullptr) {
            return RelocationResult::invalid_segment;
        }
        std::size_t patch_offset{};
        if (!detail::relocation_word_offset(relocation, segments, segment_count,
                                            memory_size, patch_offset)) {
            return RelocationResult::invalid_patch;
        }

        const auto relocate_to = guest_base + target->address;
        auto word = detail::read_u32(memory, patch_offset);
        switch (relocation.type) {
        case 0: // R_MIPS_NONE
            break;
        case 2: // R_MIPS_32
            word += relocate_to;
            break;
        case 4: { // R_MIPS_26
            const auto target_field =
                ((word & 0x03ffffffU) + (relocate_to >> 2U)) & 0x03ffffffU;
            word = (word & 0xfc000000U) | target_field;
            break;
        }
        case 5: { // R_MIPS_HI16, paired with a following LO16
            std::uint32_t low_word{};
            bool found = false;
            for (std::size_t j = i + 1; j < relocation_count; ++j) {
                if (relocations[j].type != 6 ||
                    relocations[j].target_segment != relocation.target_segment) {
                    continue;
                }
                std::size_t low_offset{};
                if (!detail::relocation_word_offset(
                        relocations[j], segments, segment_count, memory_size,
                        low_offset)) {
                    return RelocationResult::invalid_patch;
                }
                low_word = detail::read_u32(memory, low_offset);
                found = true;
                break;
            }
            if (!found) {
                return RelocationResult::missing_lo16;
            }
            auto full = (word & 0xffffU) << 16U;
            full += static_cast<std::uint32_t>(static_cast<std::int32_t>(
                static_cast<std::int16_t>(low_word & 0xffffU)));
            full += relocate_to;
            word = (word & 0xffff0000U) | ((full + 0x8000U) >> 16U);
            break;
        }
        case 6: // R_MIPS_LO16
            word = (word & 0xffff0000U) |
                   ((static_cast<std::uint32_t>(static_cast<std::int32_t>(
                         static_cast<std::int16_t>(word & 0xffffU))) +
                     relocate_to) &
                    0xffffU);
            break;
        default:
            return RelocationResult::unsupported_type;
        }
        detail::write_u32(memory, patch_offset, word);
    }
    return RelocationResult::success;
}

} // namespace psprecomp
