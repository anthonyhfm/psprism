#pragma once

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace psprecomp {

enum class StopReason : std::uint8_t {
    running,
    returned,
    step_limit,
    invalid_pc,
    unsupported_instruction,
    memory_fault,
    syscall,
    breakpoint,
};

struct State {
    std::uint32_t gpr[32]{};
    std::uint32_t hi{};
    std::uint32_t lo{};
    std::uint32_t fpr[32]{};
    std::uint32_t fcr31{};
    std::uint32_t vfpu[128]{};
    std::uint32_t vfpu_ctrl[16]{0x000000e4U, 0x000000e4U};
    std::uint32_t pc{};

    // Guest addresses [memory_base, memory_base + memory_size) map to memory.
    std::uint8_t* memory{};
    std::size_t memory_size{};
    std::uint32_t memory_base{};
    std::uint8_t* scratchpad{};
    std::size_t scratchpad_size{};
    std::uint8_t* video_memory{};
    std::size_t video_memory_size{};
    // PSP-native builds may access real user/VRAM addresses returned by the
    // firmware in addition to the relocated module image.
    bool direct_memory_access{};

    bool branch_pending{};
    std::uint32_t branch_target{};
    StopReason stop_reason{StopReason::running};
    std::uint32_t fault_address{};
    std::uint32_t fault_instruction{};
    std::uint32_t fault_pc{};
};

inline std::uint32_t canonical_address(std::uint32_t address) {
    // Allegrex exposes RAM through cached, uncached and kernel aliases.  Games
    // commonly set bit 30 on display-list pointers before writing through
    // them. Portable hosts keep one backing allocation for all aliases.
    return address & 0x1fffffffU;
}

inline std::uint8_t* mapped_address(const State& state, std::uint32_t address,
                                    std::size_t width) {
    address = canonical_address(address);
    const auto within = [width](std::uint32_t address, std::uint32_t base,
                                std::size_t size) {
        if (address < base)
            return false;
        const auto offset = static_cast<std::size_t>(address - base);
        return offset <= size && width <= size - offset;
    };
    if (state.memory != nullptr &&
        within(address, state.memory_base, state.memory_size)) {
        return state.memory + address - state.memory_base;
    }
    if (state.scratchpad != nullptr &&
        within(address, 0x00010000U, state.scratchpad_size)) {
        return state.scratchpad + address - 0x00010000U;
    }
    if (state.video_memory != nullptr &&
        within(address, 0x04000000U, state.video_memory_size)) {
        return state.video_memory + address - 0x04000000U;
    }
    return nullptr;
}

inline bool address_ok(const State& state, std::uint32_t address,
                       std::size_t width) {
    return mapped_address(state, address, width) != nullptr;
}

inline bool direct_address_ok(State& state, std::uint32_t address,
                              std::size_t width) {
    // PSP user code can access scratchpad, VRAM and RAM directly, but the
    // first 64 KiB are unmapped.  Catch null-derived accesses here instead of
    // letting the host PSP thread fault outside the recompiler runtime.
    if (address < 0x00010000U ||
        width > static_cast<std::size_t>(
                    std::numeric_limits<std::uint32_t>::max() - address) +
                    1U) {
        state.stop_reason = StopReason::memory_fault;
        state.fault_address = address;
        if (state.fault_pc == 0)
            state.fault_pc = state.pc;
        return false;
    }
    return true;
}

inline std::uint8_t load8(State& state, std::uint32_t address) {
    if (auto* pointer = mapped_address(state, address, 1)) {
        return *pointer;
    }
    if (state.direct_memory_access) {
        if (!direct_address_ok(state, address, 1)) {
            return 0;
        }
        return *reinterpret_cast<volatile std::uint8_t*>(
            static_cast<std::uintptr_t>(address));
    }
    if (!address_ok(state, address, 1)) {
        state.stop_reason = StopReason::memory_fault;
        state.fault_address = address;
        if (state.fault_pc == 0)
            state.fault_pc = state.pc;
        return 0;
    }
    return *mapped_address(state, address, 1);
}

inline std::uint16_t load16(State& state, std::uint32_t address) {
    if ((address & 1U) != 0U) {
        state.stop_reason = StopReason::memory_fault;
        state.fault_address = address;
        if (state.fault_pc == 0)
            state.fault_pc = state.pc;
        return 0;
    }
    if (auto* pointer = mapped_address(state, address, 2)) {
        return static_cast<std::uint16_t>(pointer[0]) |
               static_cast<std::uint16_t>(pointer[1]) << 8U;
    }
    if (state.direct_memory_access) {
        if (!direct_address_ok(state, address, 2)) {
            return 0;
        }
#if defined(__PSP__)
        return *reinterpret_cast<volatile std::uint16_t*>(
            static_cast<std::uintptr_t>(address));
#else
        const auto* pointer = reinterpret_cast<volatile std::uint8_t*>(
            static_cast<std::uintptr_t>(address));
        return static_cast<std::uint16_t>(pointer[0]) |
               static_cast<std::uint16_t>(pointer[1]) << 8U;
#endif
    }
    if (!address_ok(state, address, 2)) {
        state.stop_reason = StopReason::memory_fault;
        state.fault_address = address;
        if (state.fault_pc == 0)
            state.fault_pc = state.pc;
        return 0;
    }
    const auto* pointer = mapped_address(state, address, 2);
    return static_cast<std::uint16_t>(pointer[0]) |
           static_cast<std::uint16_t>(pointer[1]) << 8U;
}

inline std::uint32_t load32(State& state, std::uint32_t address) {
    if ((address & 3U) != 0U) {
        state.stop_reason = StopReason::memory_fault;
        state.fault_address = address;
        if (state.fault_pc == 0)
            state.fault_pc = state.pc;
        return 0;
    }
    if (auto* pointer = mapped_address(state, address, 4)) {
        return static_cast<std::uint32_t>(pointer[0]) |
               static_cast<std::uint32_t>(pointer[1]) << 8U |
               static_cast<std::uint32_t>(pointer[2]) << 16U |
               static_cast<std::uint32_t>(pointer[3]) << 24U;
    }
    if (state.direct_memory_access) {
        if (!direct_address_ok(state, address, 4)) {
            return 0;
        }
#if defined(__PSP__)
        return *reinterpret_cast<volatile std::uint32_t*>(
            static_cast<std::uintptr_t>(address));
#else
        const auto* pointer = reinterpret_cast<volatile std::uint8_t*>(
            static_cast<std::uintptr_t>(address));
        return static_cast<std::uint32_t>(pointer[0]) |
               static_cast<std::uint32_t>(pointer[1]) << 8U |
               static_cast<std::uint32_t>(pointer[2]) << 16U |
               static_cast<std::uint32_t>(pointer[3]) << 24U;
#endif
    }
    if (!address_ok(state, address, 4)) {
        state.stop_reason = StopReason::memory_fault;
        state.fault_address = address;
        if (state.fault_pc == 0)
            state.fault_pc = state.pc;
        return 0;
    }
    const auto* pointer = mapped_address(state, address, 4);
    return static_cast<std::uint32_t>(pointer[0]) |
           static_cast<std::uint32_t>(pointer[1]) << 8U |
           static_cast<std::uint32_t>(pointer[2]) << 16U |
           static_cast<std::uint32_t>(pointer[3]) << 24U;
}

// Instruction immediates are read from the relocated guest image.  PSP PRX
// relocation records patch fields such as LUI/LO16/J26 after the module's load
// address is known, so baking the original ELF bits into generated C++ would
// produce pointers to address zero.
inline std::uint32_t instruction_word(const State& state,
                                      std::uint32_t current_pc,
                                      std::uint32_t fallback) {
    if ((current_pc & 3U) != 0U || !address_ok(state, current_pc, 4)) {
        return fallback;
    }
    const auto* pointer = mapped_address(state, current_pc, 4);
    return static_cast<std::uint32_t>(pointer[0]) |
           static_cast<std::uint32_t>(pointer[1]) << 8U |
           static_cast<std::uint32_t>(pointer[2]) << 16U |
           static_cast<std::uint32_t>(pointer[3]) << 24U;
}

inline std::uint32_t instruction_immediate(const State& state,
                                           std::uint32_t current_pc,
                                           std::uint32_t fallback) {
    return instruction_word(state, current_pc, fallback) & 0xffffU;
}

inline std::uint32_t instruction_signed_immediate(const State& state,
                                                  std::uint32_t current_pc,
                                                  std::uint32_t fallback) {
    return static_cast<std::uint32_t>(
        static_cast<std::int32_t>(static_cast<std::int16_t>(
            instruction_immediate(state, current_pc, fallback))));
}

inline std::int32_t instruction_signed_immediate_s32(const State& state,
                                                     std::uint32_t current_pc,
                                                     std::uint32_t fallback) {
    return static_cast<std::int16_t>(
        instruction_immediate(state, current_pc, fallback));
}

inline std::uint32_t instruction_branch_target(const State& state,
                                               std::uint32_t current_pc,
                                               std::uint32_t fallback) {
    return current_pc + 4U +
           instruction_signed_immediate(state, current_pc, fallback) * 4U;
}

inline std::uint32_t instruction_jump_target(const State& state,
                                             std::uint32_t current_pc,
                                             std::uint32_t fallback) {
    const auto instruction = instruction_word(state, current_pc, fallback);
    return ((current_pc + 4U) & 0xf0000000U) |
           ((instruction & 0x03ffffffU) << 2U);
}

inline void store8(State& state, std::uint32_t address, std::uint8_t value) {
    if (auto* pointer = mapped_address(state, address, 1)) {
        *pointer = value;
        return;
    }
    if (state.direct_memory_access) {
        if (!direct_address_ok(state, address, 1)) {
            return;
        }
        *reinterpret_cast<volatile std::uint8_t*>(
            static_cast<std::uintptr_t>(address)) = value;
        return;
    }
    if (!address_ok(state, address, 1)) {
        state.stop_reason = StopReason::memory_fault;
        state.fault_address = address;
        if (state.fault_pc == 0)
            state.fault_pc = state.pc;
        return;
    }
    *mapped_address(state, address, 1) = value;
}

inline void store16(State& state, std::uint32_t address, std::uint16_t value) {
    if ((address & 1U) != 0U) {
        state.stop_reason = StopReason::memory_fault;
        state.fault_address = address;
        if (state.fault_pc == 0)
            state.fault_pc = state.pc;
        return;
    }
    if (auto* pointer = mapped_address(state, address, 2)) {
        pointer[0] = static_cast<std::uint8_t>(value);
        pointer[1] = static_cast<std::uint8_t>(value >> 8U);
        return;
    }
    if (state.direct_memory_access) {
        if (!direct_address_ok(state, address, 2)) {
            return;
        }
#if defined(__PSP__)
        *reinterpret_cast<volatile std::uint16_t*>(
            static_cast<std::uintptr_t>(address)) = value;
#else
        auto* pointer = reinterpret_cast<volatile std::uint8_t*>(
            static_cast<std::uintptr_t>(address));
        pointer[0] = static_cast<std::uint8_t>(value);
        pointer[1] = static_cast<std::uint8_t>(value >> 8U);
#endif
        return;
    }
    if (!address_ok(state, address, 2)) {
        state.stop_reason = StopReason::memory_fault;
        state.fault_address = address;
        if (state.fault_pc == 0)
            state.fault_pc = state.pc;
        return;
    }
    auto* pointer = mapped_address(state, address, 2);
    pointer[0] = static_cast<std::uint8_t>(value);
    pointer[1] = static_cast<std::uint8_t>(value >> 8U);
}

inline void store32(State& state, std::uint32_t address, std::uint32_t value) {
    if ((address & 3U) != 0U) {
        state.stop_reason = StopReason::memory_fault;
        state.fault_address = address;
        if (state.fault_pc == 0)
            state.fault_pc = state.pc;
        return;
    }
    if (auto* pointer = mapped_address(state, address, 4)) {
        pointer[0] = static_cast<std::uint8_t>(value);
        pointer[1] = static_cast<std::uint8_t>(value >> 8U);
        pointer[2] = static_cast<std::uint8_t>(value >> 16U);
        pointer[3] = static_cast<std::uint8_t>(value >> 24U);
        return;
    }
    if (state.direct_memory_access) {
        if (!direct_address_ok(state, address, 4)) {
            return;
        }
#if defined(__PSP__)
        *reinterpret_cast<volatile std::uint32_t*>(
            static_cast<std::uintptr_t>(address)) = value;
#else
        auto* pointer = reinterpret_cast<volatile std::uint8_t*>(
            static_cast<std::uintptr_t>(address));
        pointer[0] = static_cast<std::uint8_t>(value);
        pointer[1] = static_cast<std::uint8_t>(value >> 8U);
        pointer[2] = static_cast<std::uint8_t>(value >> 16U);
        pointer[3] = static_cast<std::uint8_t>(value >> 24U);
#endif
        return;
    }
    if (!address_ok(state, address, 4)) {
        state.stop_reason = StopReason::memory_fault;
        state.fault_address = address;
        if (state.fault_pc == 0)
            state.fault_pc = state.pc;
        return;
    }
    auto* pointer = mapped_address(state, address, 4);
    pointer[0] = static_cast<std::uint8_t>(value);
    pointer[1] = static_cast<std::uint8_t>(value >> 8U);
    pointer[2] = static_cast<std::uint8_t>(value >> 16U);
    pointer[3] = static_cast<std::uint8_t>(value >> 24U);
}

#if defined(__PSP__)
inline std::uint8_t direct_load8(std::uint32_t address) {
    return *reinterpret_cast<volatile std::uint8_t*>(
        static_cast<std::uintptr_t>(address));
}
inline std::uint16_t direct_load16(std::uint32_t address) {
    return *reinterpret_cast<volatile std::uint16_t*>(
        static_cast<std::uintptr_t>(address));
}
inline std::uint32_t direct_load32(std::uint32_t address) {
    return *reinterpret_cast<volatile std::uint32_t*>(
        static_cast<std::uintptr_t>(address));
}
inline void direct_store8(std::uint32_t address, std::uint8_t value) {
    *reinterpret_cast<volatile std::uint8_t*>(
        static_cast<std::uintptr_t>(address)) = value;
}
inline void direct_store16(std::uint32_t address, std::uint16_t value) {
    *reinterpret_cast<volatile std::uint16_t*>(
        static_cast<std::uintptr_t>(address)) = value;
}
inline void direct_store32(std::uint32_t address, std::uint32_t value) {
    *reinterpret_cast<volatile std::uint32_t*>(
        static_cast<std::uintptr_t>(address)) = value;
}
#define PSPRECOMP_LOAD8(state, address) ::psprecomp::load8(state, address)
#define PSPRECOMP_LOAD16(state, address) ::psprecomp::load16(state, address)
#define PSPRECOMP_LOAD32(state, address) ::psprecomp::load32(state, address)
#define PSPRECOMP_STORE8(state, address, value)                                \
    ::psprecomp::store8(state, address, value)
#define PSPRECOMP_STORE16(state, address, value)                               \
    ::psprecomp::store16(state, address, value)
#define PSPRECOMP_STORE32(state, address, value)                               \
    ::psprecomp::store32(state, address, value)
#else
#define PSPRECOMP_LOAD8(state, address) ::psprecomp::load8(state, address)
#define PSPRECOMP_LOAD16(state, address) ::psprecomp::load16(state, address)
#define PSPRECOMP_LOAD32(state, address) ::psprecomp::load32(state, address)
#define PSPRECOMP_STORE8(state, address, value)                                \
    ::psprecomp::store8(state, address, value)
#define PSPRECOMP_STORE16(state, address, value)                               \
    ::psprecomp::store16(state, address, value)
#define PSPRECOMP_STORE32(state, address, value)                               \
    ::psprecomp::store32(state, address, value)
#endif

inline std::uint32_t load_word_left(State& state, std::uint32_t address,
                                    std::uint32_t value) {
    const auto offset = address & 3U;
    const auto aligned = address & ~3U;
    for (std::uint32_t i = 0; i <= offset; ++i) {
        const auto shift = 8U * (3U - offset + i);
        const auto mask = 0xffU << shift;
        value = (value & ~mask) |
                (static_cast<std::uint32_t>(
                     PSPRECOMP_LOAD8(state, aligned + i))
                 << shift);
    }
    return value;
}

inline std::uint32_t load_word_right(State& state, std::uint32_t address,
                                     std::uint32_t value) {
    const auto offset = address & 3U;
    const auto aligned = address & ~3U;
    for (std::uint32_t i = offset; i < 4U; ++i) {
        const auto shift = 8U * (i - offset);
        const auto mask = 0xffU << shift;
        value = (value & ~mask) |
                (static_cast<std::uint32_t>(
                     PSPRECOMP_LOAD8(state, aligned + i))
                 << shift);
    }
    return value;
}

inline void store_word_left(State& state, std::uint32_t address,
                            std::uint32_t value) {
    const auto offset = address & 3U;
    const auto aligned = address & ~3U;
    for (std::uint32_t i = 0; i <= offset; ++i) {
        const auto shift = 8U * (3U - offset + i);
        store8(state, aligned + i, static_cast<std::uint8_t>(value >> shift));
    }
}

inline void store_word_right(State& state, std::uint32_t address,
                             std::uint32_t value) {
    const auto offset = address & 3U;
    const auto aligned = address & ~3U;
    for (std::uint32_t i = offset; i < 4U; ++i) {
        const auto shift = 8U * (i - offset);
        store8(state, aligned + i, static_cast<std::uint8_t>(value >> shift));
    }
}

inline std::int32_t as_s32(std::uint32_t value) {
    return static_cast<std::int32_t>(value);
}

inline std::uint32_t arithmetic_shift_right(std::uint32_t value,
                                            std::uint32_t amount) {
    amount &= 31U;
    if (amount == 0) {
        return value;
    }
    const auto shifted = value >> amount;
    return (value & 0x80000000U) == 0
               ? shifted
               : shifted | (~std::uint32_t{0} << (32U - amount));
}

inline std::uint32_t rotate_right(std::uint32_t value, std::uint32_t amount) {
    amount &= 31U;
    return amount == 0 ? value : (value >> amount) | (value << (32U - amount));
}

inline float f32(const State& state, std::uint32_t index) {
    return std::bit_cast<float>(state.fpr[index & 31U]);
}

inline void set_f32(State& state, std::uint32_t index, float value) {
    state.fpr[index & 31U] = std::bit_cast<std::uint32_t>(value);
}

inline bool fpu_condition(const State& state, std::uint32_t cc) {
    const auto bit = cc == 0 ? 23U : 24U + (cc & 7U);
    return (state.fcr31 & (1U << bit)) != 0;
}

inline void set_fpu_condition(State& state, std::uint32_t cc, bool value) {
    const auto bit = cc == 0 ? 23U : 24U + (cc & 7U);
    const auto mask = 1U << bit;
    state.fcr31 = value ? state.fcr31 | mask : state.fcr31 & ~mask;
}

inline void compare_f32(State& state, std::uint32_t cc, float left, float right,
                        std::uint32_t condition) {
    const bool unordered = std::isnan(left) || std::isnan(right);
    const bool less = !unordered && left < right;
    const bool equal = !unordered && left == right;
    const bool result = ((condition & 4U) != 0U && less) ||
                        ((condition & 2U) != 0U && equal) ||
                        ((condition & 1U) != 0U && unordered);
    set_fpu_condition(state, cc, result);
}

inline std::uint32_t rounded_word(float value, std::uint32_t mode) {
    double rounded{};
    switch (mode & 3U) {
    case 0:
        rounded = std::nearbyint(static_cast<double>(value));
        break;
    case 1:
        rounded = std::trunc(static_cast<double>(value));
        break;
    case 2:
        rounded = std::ceil(static_cast<double>(value));
        break;
    default:
        rounded = std::floor(static_cast<double>(value));
        break;
    }
    if (!std::isfinite(rounded) ||
        rounded <
            static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
        rounded >
            static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
        return 0x80000000U;
    }
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(rounded));
}

inline std::uint32_t reverse_bits(std::uint32_t value) {
    value = ((value & 0x55555555U) << 1U) | ((value & 0xAAAAAAAAU) >> 1U);
    value = ((value & 0x33333333U) << 2U) | ((value & 0xCCCCCCCCU) >> 2U);
    value = ((value & 0x0F0F0F0FU) << 4U) | ((value & 0xF0F0F0F0U) >> 4U);
    value = ((value & 0x00FF00FFU) << 8U) | ((value & 0xFF00FF00U) >> 8U);
    return (value << 16U) | (value >> 16U);
}

inline std::uint32_t byte_swap(std::uint32_t value) {
    return ((value & 0x000000FFU) << 24U) |
           ((value & 0x0000FF00U) << 8U)  |
           ((value & 0x00FF0000U) >> 8U)  |
           ((value & 0xFF000000U) >> 24U);
}

} // namespace psprecomp
