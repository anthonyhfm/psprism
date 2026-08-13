#pragma once

#include <psprecomp/vfpu.hpp>

#include <array>
#include <cstdint>
#include <string_view>

namespace psprecomp {

enum class InstructionLowering : std::uint8_t {
    native,
    guarded_native,
    runtime_fallback,
    invalid,
};

enum InstructionFlag : std::uint8_t {
    instruction_none = 0,
    instruction_delayed_branch = 1U << 0U,
    instruction_likely = 1U << 1U,
    instruction_call = 1U << 2U,
    instruction_dynamic_target = 1U << 3U,
};

struct DecodedInstruction {
    std::uint32_t word{};
    std::uint32_t op{};
    std::uint32_t rs{};
    std::uint32_t rt{};
    std::uint32_t rd{};
    std::uint32_t shift{};
    std::uint32_t function{};
    InstructionLowering lowering{InstructionLowering::invalid};
    std::uint8_t flags{};
    std::string_view name{"invalid"};

    [[nodiscard]] constexpr bool valid() const {
        return lowering != InstructionLowering::invalid;
    }
    [[nodiscard]] constexpr bool has_flag(InstructionFlag flag) const {
        return (flags & static_cast<std::uint8_t>(flag)) != 0U;
    }
};

namespace detail {

struct InstructionPattern {
    std::uint32_t mask;
    std::uint32_t match;
    std::string_view name;
};

// The table is deliberately shared by the translator and interpreter.  More
// specific Allegrex encodings precede their generic MIPS relatives.
inline constexpr std::array special_patterns{
    InstructionPattern{0xffe0003fU, 0x00200002U, "rotr"},
    InstructionPattern{0xfc0007ffU, 0x00000046U, "rotrv"},
    InstructionPattern{0xffe0003fU, 0x00000002U, "srl"},
    InstructionPattern{0xfc0007ffU, 0x00000006U, "srlv"},
    InstructionPattern{0xffe0003fU, 0x00000000U, "sll"},
    InstructionPattern{0xffe0003fU, 0x00000003U, "sra"},
    InstructionPattern{0xfc0007ffU, 0x00000004U, "sllv"},
    InstructionPattern{0xfc0007ffU, 0x00000007U, "srav"},
    InstructionPattern{0xfc00003fU, 0x00000008U, "jr"},
    InstructionPattern{0xfc00003fU, 0x00000009U, "jalr"},
    InstructionPattern{0xfc00003fU, 0x0000000aU, "movz"},
    InstructionPattern{0xfc00003fU, 0x0000000bU, "movn"},
    InstructionPattern{0xfc00003fU, 0x0000000cU, "syscall"},
    InstructionPattern{0xfc00003fU, 0x0000000dU, "break"},
    InstructionPattern{0xfc00003fU, 0x0000000fU, "sync"},
    InstructionPattern{0xfc00003fU, 0x00000010U, "mfhi"},
    InstructionPattern{0xfc00003fU, 0x00000011U, "mthi"},
    InstructionPattern{0xfc00003fU, 0x00000012U, "mflo"},
    InstructionPattern{0xfc00003fU, 0x00000013U, "mtlo"},
    InstructionPattern{0xfc00003fU, 0x00000016U, "clz"},
    InstructionPattern{0xfc00003fU, 0x00000017U, "clo"},
    InstructionPattern{0xfc00003fU, 0x00000018U, "mult"},
    InstructionPattern{0xfc00003fU, 0x00000019U, "multu"},
    InstructionPattern{0xfc00003fU, 0x0000001aU, "div"},
    InstructionPattern{0xfc00003fU, 0x0000001bU, "divu"},
    InstructionPattern{0xfc00003fU, 0x0000001cU, "madd"},
    InstructionPattern{0xfc00003fU, 0x0000001dU, "maddu"},
    InstructionPattern{0xfc00003fU, 0x00000020U, "add"},
    InstructionPattern{0xfc00003fU, 0x00000021U, "addu"},
    InstructionPattern{0xfc00003fU, 0x00000022U, "sub"},
    InstructionPattern{0xfc00003fU, 0x00000023U, "subu"},
    InstructionPattern{0xfc00003fU, 0x00000024U, "and"},
    InstructionPattern{0xfc00003fU, 0x00000025U, "or"},
    InstructionPattern{0xfc00003fU, 0x00000026U, "xor"},
    InstructionPattern{0xfc00003fU, 0x00000027U, "nor"},
    InstructionPattern{0xfc00003fU, 0x0000002aU, "slt"},
    InstructionPattern{0xfc00003fU, 0x0000002bU, "sltu"},
    InstructionPattern{0xfc00003fU, 0x0000002cU, "max"},
    InstructionPattern{0xfc00003fU, 0x0000002dU, "min"},
    InstructionPattern{0xfc00003fU, 0x0000002eU, "msub"},
    InstructionPattern{0xfc00003fU, 0x0000002fU, "msubu"},
};

inline constexpr std::array regimm_patterns{
    InstructionPattern{0xfc1f0000U, 0x04000000U, "bltz"},
    InstructionPattern{0xfc1f0000U, 0x04010000U, "bgez"},
    InstructionPattern{0xfc1f0000U, 0x04020000U, "bltzl"},
    InstructionPattern{0xfc1f0000U, 0x04030000U, "bgezl"},
    InstructionPattern{0xfc1f0000U, 0x04100000U, "bltzal"},
    InstructionPattern{0xfc1f0000U, 0x04110000U, "bgezal"},
    InstructionPattern{0xfc1f0000U, 0x04120000U, "bltzall"},
    InstructionPattern{0xfc1f0000U, 0x04130000U, "bgezall"},
};

template <std::size_t Size>
[[nodiscard]] constexpr const InstructionPattern* find_pattern(
    std::uint32_t word, const std::array<InstructionPattern, Size>& patterns) {
    for (const auto& pattern : patterns) {
        if ((word & pattern.mask) == pattern.match) {
            return &pattern;
        }
    }
    return nullptr;
}

} // namespace detail

[[nodiscard]] constexpr DecodedInstruction
decode_allegrex(std::uint32_t word) {
    DecodedInstruction result{
        word,
        word >> 26U,
        (word >> 21U) & 31U,
        (word >> 16U) & 31U,
        (word >> 11U) & 31U,
        (word >> 6U) & 31U,
        word & 63U,
        InstructionLowering::invalid,
        instruction_none,
        "invalid",
    };

    if (vfpu_opcode_supported(word)) {
        const auto operation = vfpu_static_operation(word);
        result.lowering = operation == VfpuStaticOperation::none
                              ? InstructionLowering::runtime_fallback
                              : InstructionLowering::guarded_native;
        switch (operation) {
        case VfpuStaticOperation::add: result.name = "vadd"; break;
        case VfpuStaticOperation::subtract: result.name = "vsub"; break;
        case VfpuStaticOperation::multiply: result.name = "vmul"; break;
        case VfpuStaticOperation::dot: result.name = "vdot"; break;
        case VfpuStaticOperation::move: result.name = "vmov"; break;
        default: result.name = "vfpu"; break;
        }
        if (result.op == 0x12U && result.rs == 0x08U) {
            result.flags = instruction_delayed_branch;
            if ((result.rt & 2U) != 0U) {
                result.flags |= instruction_likely;
            }
        }
        return result;
    }

    if (result.op == 0U) {
        if (const auto* pattern =
                detail::find_pattern(word, detail::special_patterns)) {
            result.lowering = InstructionLowering::native;
            result.name = pattern->name;
            if (result.function == 0x08U || result.function == 0x09U) {
                result.flags = instruction_delayed_branch |
                               instruction_dynamic_target;
                if (result.function == 0x09U) {
                    result.flags |= instruction_call;
                }
            }
        }
        return result;
    }

    if (result.op == 1U) {
        if (const auto* pattern =
                detail::find_pattern(word, detail::regimm_patterns)) {
            result.lowering = InstructionLowering::native;
            result.name = pattern->name;
            result.flags = instruction_delayed_branch;
            if (result.rt == 2U || result.rt == 3U || result.rt == 0x12U ||
                result.rt == 0x13U) {
                result.flags |= instruction_likely;
            }
            if (result.rt >= 0x10U) {
                result.flags |= instruction_call;
            }
        }
        return result;
    }

    const auto native = [&](std::string_view name,
                            std::uint8_t flags = instruction_none) {
        result.lowering = InstructionLowering::native;
        result.name = name;
        result.flags = flags;
    };
    switch (result.op) {
    case 0x02: native("j", instruction_delayed_branch); break;
    case 0x03:
        native("jal", instruction_delayed_branch | instruction_call);
        break;
    case 0x04: native("beq", instruction_delayed_branch); break;
    case 0x05: native("bne", instruction_delayed_branch); break;
    case 0x06: native("blez", instruction_delayed_branch); break;
    case 0x07: native("bgtz", instruction_delayed_branch); break;
    case 0x08: native("addi"); break;
    case 0x09: native("addiu"); break;
    case 0x0a: native("slti"); break;
    case 0x0b: native("sltiu"); break;
    case 0x0c: native("andi"); break;
    case 0x0d: native("ori"); break;
    case 0x0e: native("xori"); break;
    case 0x0f: native("lui"); break;
    case 0x11: {
        const bool scalar_operation =
            result.rs == 0x10U &&
            ((result.function <= 0x07U) ||
             (result.function >= 0x0cU && result.function <= 0x0fU) ||
             result.function == 0x11U || result.function == 0x20U ||
             result.function == 0x24U || result.function >= 0x30U);
        const bool word_to_single =
            result.rs == 0x14U && result.function == 0x20U;
        if (result.rs == 0x00U || result.rs == 0x02U ||
            result.rs == 0x04U || result.rs == 0x06U || scalar_operation ||
            word_to_single) {
            native("cop1");
        } else if (result.rs == 0x08U) {
            native("bc1", instruction_delayed_branch |
                              ((result.rt & 2U) != 0U ? instruction_likely
                                                     : instruction_none));
        }
        break;
    }
    case 0x14:
        native("beql", instruction_delayed_branch | instruction_likely);
        break;
    case 0x15:
        native("bnel", instruction_delayed_branch | instruction_likely);
        break;
    case 0x16:
        native("blezl", instruction_delayed_branch | instruction_likely);
        break;
    case 0x17:
        native("bgtzl", instruction_delayed_branch | instruction_likely);
        break;
    case 0x1c:
        if (result.function == 0x20U || result.function == 0x21U ||
            result.function == 0x2cU || result.function == 0x2dU) {
            native("special2");
        }
        break;
    case 0x1f:
        if (result.function == 0U ||
            (result.function == 4U && result.rd >= result.shift) ||
            (result.function == 0x20U &&
             (result.shift == 0x02U || result.shift == 0x03U ||
              result.shift == 0x10U || result.shift == 0x14U ||
              result.shift == 0x18U))) {
            native("special3");
        }
        break;
    case 0x20: native("lb"); break;
    case 0x21: native("lh"); break;
    case 0x22: native("lwl"); break;
    case 0x23: native("lw"); break;
    case 0x24: native("lbu"); break;
    case 0x25: native("lhu"); break;
    case 0x26: native("lwr"); break;
    case 0x28: native("sb"); break;
    case 0x29: native("sh"); break;
    case 0x2a: native("swl"); break;
    case 0x2b: native("sw"); break;
    case 0x2e: native("swr"); break;
    case 0x2f: native("cache"); break;
    case 0x30: native("ll"); break;
    case 0x31: native("lwc1"); break;
    case 0x38: native("sc"); break;
    case 0x39: native("swc1"); break;
    default: break;
    }
    return result;
}

} // namespace psprecomp
