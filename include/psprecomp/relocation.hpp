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
    invalid_stream,
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

class CompactRelocationDecoder {
  public:
    CompactRelocationDecoder(const std::uint8_t* bytes, std::size_t size)
        : bytes_(bytes), size_(size) {}

    bool next(PspRelocationRecord& relocation) {
        if (cursor_ >= size_) {
            valid_ = false;
            return false;
        }
        const auto first = bytes_[cursor_++];
        std::int64_t delta{};
        if (first != 0xffU) {
            switch (first & 0x07U) {
            case 0:
                type_ = 0U;
                break;
            case 1:
                type_ = 2U;
                break;
            case 2:
                type_ = 4U;
                break;
            case 3:
                type_ = 5U;
                break;
            case 4:
                type_ = 6U;
                break;
            case 5:
                type_ = 2U;
                patch_segment_ = 0U;
                target_segment_ = 0U;
                delta = static_cast<std::int64_t>(first >> 3U) * 4;
                has_metadata_ = true;
                break;
            case 6:
                type_ = 4U;
                patch_segment_ = 0U;
                target_segment_ = 0U;
                delta = static_cast<std::int64_t>(first >> 3U) * 4;
                has_metadata_ = true;
                break;
            case 7:
                type_ = 6U;
                patch_segment_ = 0U;
                target_segment_ = 0U;
                delta = static_cast<std::int64_t>(first >> 3U) * 4;
                has_metadata_ = true;
                break;
            }
            if ((first & 0x07U) <= 4U) {
                patch_segment_ = (first >> 3U) & 1U;
                target_segment_ = (first >> 4U) & 1U;
                delta = static_cast<std::int64_t>(first >> 5U) * 4;
                has_metadata_ = true;
            }
        } else {
            std::uint64_t tag{};
            unsigned shift = 0;
            while (true) {
                if (cursor_ >= size_ || shift >= 63U) {
                    valid_ = false;
                    return false;
                }
                const auto byte = bytes_[cursor_++];
                tag |= static_cast<std::uint64_t>(byte & 0x7fU) << shift;
                if ((byte & 0x80U) == 0U) {
                    break;
                }
                shift += 7U;
            }

            if ((tag & 1U) != 0U) {
                if (cursor_ >= size_) {
                    valid_ = false;
                    return false;
                }
                const auto packed = bytes_[cursor_++];
                if (packed != 0xffU) {
                    type_ = packed & 0x0fU;
                    patch_segment_ = (packed >> 4U) & 0x03U;
                    target_segment_ = packed >> 6U;
                } else {
                    if (cursor_ > size_ || size_ - cursor_ < 3U) {
                        valid_ = false;
                        return false;
                    }
                    type_ = bytes_[cursor_++];
                    patch_segment_ = bytes_[cursor_++];
                    target_segment_ = bytes_[cursor_++];
                }
                has_metadata_ = true;
            }
            if (!has_metadata_) {
                valid_ = false;
                return false;
            }

            const auto zigzag = tag >> 1U;
            const auto magnitude = zigzag >> 1U;
            delta = (zigzag & 1U) != 0U
                        ? -static_cast<std::int64_t>(magnitude + 1U)
                        : static_cast<std::int64_t>(magnitude);
        }

        const auto next_offset =
            static_cast<std::int64_t>(previous_offsets_[patch_segment_]) +
            delta;
        if (next_offset < 0 || next_offset > 0xffffffffLL) {
            valid_ = false;
            return false;
        }
        previous_offsets_[patch_segment_] =
            static_cast<std::uint32_t>(next_offset);
        relocation = {static_cast<std::uint32_t>(next_offset), type_,
                      patch_segment_, target_segment_, 0};
        return true;
    }

    [[nodiscard]] bool valid() const { return valid_; }
    [[nodiscard]] bool consumed(std::size_t relocation_count) const {
        return valid_ && decoded_ == relocation_count && cursor_ == size_;
    }
    void note_decoded() { ++decoded_; }

  private:
    const std::uint8_t* bytes_{};
    std::size_t size_{};
    std::size_t cursor_{};
    std::size_t decoded_{};
    std::uint32_t previous_offsets_[256]{};
    std::uint8_t type_{};
    std::uint8_t patch_segment_{};
    std::uint8_t target_segment_{};
    bool has_metadata_{};
    bool valid_{true};
};

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

// Applies the same relocation stream from a delta/metadata encoded byte
// sequence. The compact form is intended for native PSP wrappers, where the
// ordinary eight-byte records would permanently consume scarce game RAM.
inline RelocationResult apply_compact_psp_relocations(
    std::uint8_t* memory, std::size_t memory_size, std::uint32_t guest_base,
    const PspLoadSegment* segments, std::size_t segment_count,
    const std::uint8_t* bytes, std::size_t byte_count,
    std::size_t relocation_count) {
    if (memory == nullptr || segments == nullptr ||
        (byte_count != 0U && bytes == nullptr)) {
        return RelocationResult::invalid_patch;
    }

    detail::CompactRelocationDecoder decoder(bytes, byte_count);
    for (std::size_t i = 0; i < relocation_count; ++i) {
        PspRelocationRecord relocation;
        if (!decoder.next(relocation)) {
            return RelocationResult::invalid_stream;
        }
        decoder.note_decoded();
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
        case 0:
            break;
        case 2:
            word += relocate_to;
            break;
        case 4: {
            const auto target_field =
                ((word & 0x03ffffffU) + (relocate_to >> 2U)) & 0x03ffffffU;
            word = (word & 0xfc000000U) | target_field;
            break;
        }
        case 5: {
            auto lookahead = decoder;
            std::uint32_t low_word{};
            bool found = false;
            for (std::size_t j = i + 1U; j < relocation_count; ++j) {
                PspRelocationRecord low;
                if (!lookahead.next(low)) {
                    return RelocationResult::invalid_stream;
                }
                lookahead.note_decoded();
                if (low.type != 6U ||
                    low.target_segment != relocation.target_segment) {
                    continue;
                }
                std::size_t low_offset{};
                if (!detail::relocation_word_offset(
                        low, segments, segment_count, memory_size,
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
        case 6:
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
    return decoder.consumed(relocation_count)
               ? RelocationResult::success
               : RelocationResult::invalid_stream;
}

} // namespace psprecomp
