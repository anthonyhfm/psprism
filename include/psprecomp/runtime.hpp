#pragma once

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace psprecomp {

#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
inline constexpr bool is_big_endian = true;
#else
inline constexpr bool is_big_endian = false;
#endif

#if defined(PSPRECOMP_PROFILE_CPU) || defined(PSPRECOMP_PROFILE_DISPATCH)
inline constexpr bool cpu_profiling_compiled = true;
#else
inline constexpr bool cpu_profiling_compiled = false;
#endif

template <typename T>
constexpr T byteswap(T value) noexcept {
    if constexpr (sizeof(T) == 2) {
        return static_cast<T>((static_cast<std::uint16_t>(value) >> 8U) |
                              (static_cast<std::uint16_t>(value) << 8U));
    } else if constexpr (sizeof(T) == 4) {
        const auto v = static_cast<std::uint32_t>(value);
        return static_cast<T>((v >> 24U) |
                              ((v >> 8U) & 0x0000ff00U) |
                              ((v << 8U) & 0x00ff0000U) |
                              (v << 24U));
    } else if constexpr (sizeof(T) == 8) {
        const auto v = static_cast<std::uint64_t>(value);
        return static_cast<T>((v >> 56U) |
                              ((v >> 40U) & 0x000000000000ff00ULL) |
                              ((v >> 24U) & 0x0000000000ff0000ULL) |
                              ((v >> 8U)  & 0x00000000ff000000ULL) |
                              ((v << 8U)  & 0x000000ff00000000ULL) |
                              ((v << 24U) & 0x0000ff0000000000ULL) |
                              ((v << 40U) & 0x00ff000000000000ULL) |
                              (v << 56U));
    } else {
        return value;
    }
}

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

struct CpuProfileCounters {
    std::uint64_t dispatches{};
    std::uint64_t import_dispatches{};
    std::uint64_t translated_blocks{};
    std::uint64_t interpreter_fallbacks{};
    std::uint64_t direct_cfg_edges{};
    std::uint64_t memory_reads{};
    std::uint64_t memory_writes{};
    std::uint64_t memory_faults{};
    std::uint64_t vfpu_static_lowerings{};
    std::uint64_t vfpu_helper_fallbacks{};
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
    std::uint8_t* volatile_memory{};
    std::size_t volatile_memory_size{};
    // PSP-native builds may access real user/VRAM addresses returned by the
    // firmware in addition to the relocated module image.
    bool direct_memory_access{};

    bool branch_pending{};
    std::uint32_t branch_target{};
    StopReason stop_reason{StopReason::running};
    std::uint32_t fault_address{};
    std::uint32_t fault_instruction{};
    std::uint32_t fault_pc{};
    // Profiling is owned by the guest State, so independent guest threads do
    // not contend on process-global counters.  The disabled branch is the
    // only cost in normal builds.
    // A profiling build is opt-out per state.  This makes newly-created guest
    // thread/callback states observable without process-global counters while
    // normal builds still compile every counter update away.
    bool cpu_profile_enabled{cpu_profiling_compiled};
    std::uint32_t dispatch_route_hint{0xffffffffU};
    CpuProfileCounters cpu_profile{};
};

inline void note_cpu_dispatch(State& state) {
    if constexpr (cpu_profiling_compiled) {
        if (state.cpu_profile_enabled) {
            ++state.cpu_profile.dispatches;
        }
    }
}

inline void note_cpu_import_dispatch(State& state) {
    if constexpr (cpu_profiling_compiled) {
        if (state.cpu_profile_enabled) {
            ++state.cpu_profile.import_dispatches;
        }
    }
}

inline void note_cpu_translated_block(State& state) {
    if constexpr (cpu_profiling_compiled) {
        if (state.cpu_profile_enabled) {
            ++state.cpu_profile.translated_blocks;
        }
    }
}

inline void note_cpu_interpreter_fallback(State& state) {
    if constexpr (cpu_profiling_compiled) {
        if (state.cpu_profile_enabled) {
            ++state.cpu_profile.interpreter_fallbacks;
        }
    }
}

inline void note_cpu_direct_cfg_edge(State& state) {
    if constexpr (cpu_profiling_compiled) {
        if (state.cpu_profile_enabled) {
            ++state.cpu_profile.direct_cfg_edges;
        }
    }
}

inline void note_vfpu_static_lowering(State& state) {
    if constexpr (cpu_profiling_compiled) {
        if (state.cpu_profile_enabled) {
            ++state.cpu_profile.vfpu_static_lowerings;
        }
    }
}

inline void note_vfpu_helper_fallback(State& state) {
    if constexpr (cpu_profiling_compiled) {
        if (state.cpu_profile_enabled) {
            ++state.cpu_profile.vfpu_helper_fallbacks;
        }
    }
}

inline std::uint32_t canonical_address(std::uint32_t address) {
    // Allegrex exposes RAM through cached, uncached and kernel aliases.  Games
    // commonly set bit 30 on display-list pointers before writing through
    // them. Portable hosts keep one backing allocation for all aliases.
    return address & 0x1fffffffU;
}

inline std::uint8_t* region_address(std::uint8_t* host,
                                    std::size_t region_size,
                                    std::uint32_t region_base,
                                    std::uint32_t address,
                                    std::size_t width) {
    if (host == nullptr || address < region_base) {
        return nullptr;
    }
    const auto offset = static_cast<std::size_t>(address - region_base);
    return offset <= region_size && width <= region_size - offset
               ? host + offset
               : nullptr;
}

inline std::uint8_t* mapped_address(const State& state, std::uint32_t address,
                                    std::size_t width) {
    const std::uint32_t canon = address & 0x1fffffffU;
    const std::uint32_t offset = canon - state.memory_base;
    if (state.memory != nullptr && offset <= state.memory_size &&
        width <= state.memory_size - offset) [[likely]] {
        return state.memory + offset;
    }

    switch (canon >> 24U) {
    case 0x00U:
        return region_address(state.scratchpad, state.scratchpad_size,
                              0x00010000U, canon, width);
    case 0x04U:
        return region_address(state.video_memory, state.video_memory_size,
                              0x04000000U, canon, width);
    case 0x08U:
        return region_address(state.volatile_memory, state.volatile_memory_size,
                              0x08400000U, canon, width);
    default:
        return nullptr;
    }
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
        if constexpr (cpu_profiling_compiled) {
            if (state.cpu_profile_enabled) {
                ++state.cpu_profile.memory_faults;
            }
        }
        return false;
    }
    return true;
}

inline void note_memory_fault(State& state, std::uint32_t address) {
    state.stop_reason = StopReason::memory_fault;
    state.fault_address = address;
    if (state.fault_pc == 0) {
        state.fault_pc = state.pc;
    }
    if constexpr (cpu_profiling_compiled) {
        if (state.cpu_profile_enabled) {
            ++state.cpu_profile.memory_faults;
        }
    }
}

inline std::uint8_t load8(State& state, std::uint32_t address) {
    if constexpr (cpu_profiling_compiled) {
        if (state.cpu_profile_enabled) {
            ++state.cpu_profile.memory_reads;
        }
    }
    const std::uint32_t canon = address & 0x1fffffffU;
    const std::uint32_t offset = canon - state.memory_base;
    if (state.memory != nullptr && offset < state.memory_size) [[likely]] {
        return state.memory[offset];
    }
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
    note_memory_fault(state, address);
    return 0;
}

inline std::uint16_t byte_swap16(std::uint16_t value) {
    return static_cast<std::uint16_t>((value >> 8U) | (value << 8U));
}

inline std::uint32_t byte_swap(std::uint32_t value) {
    return ((value & 0x000000FFU) << 24U) |
           ((value & 0x0000FF00U) << 8U)  |
           ((value & 0x00FF0000U) >> 8U)  |
           ((value & 0xFF000000U) >> 24U);
}

inline std::uint16_t load16(State& state, std::uint32_t address) {
    if constexpr (cpu_profiling_compiled) {
        if (state.cpu_profile_enabled) {
            ++state.cpu_profile.memory_reads;
        }
    }
    const std::uint32_t canon = address & 0x1fffffffU;
    const std::uint32_t offset = canon - state.memory_base;
    if (state.memory != nullptr && offset <= state.memory_size - 2U && (address & 1U) == 0U) [[likely]] {
        std::uint16_t value{};
        std::memcpy(&value, state.memory + offset, sizeof(value));
        if constexpr (is_big_endian) {
            value = byte_swap16(value);
        }
        return value;
    }
    if ((address & 1U) != 0U) {
        note_memory_fault(state, address);
        return 0;
    }
    if (const auto* pointer = mapped_address(state, address, 2)) {
        std::uint16_t value{};
        std::memcpy(&value, pointer, sizeof(value));
        if constexpr (is_big_endian) {
            value = byte_swap16(value);
        }
        return value;
    }
    if (state.direct_memory_access) {
        if (!direct_address_ok(state, address, 2)) {
            return 0;
        }
#if defined(__PSP__)
        return *reinterpret_cast<volatile std::uint16_t*>(
            static_cast<std::uintptr_t>(address));
#else
        const auto* pointer = reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(address));
        std::uint16_t value{};
        std::memcpy(&value, pointer, sizeof(value));
        if constexpr (is_big_endian) {
            value = byte_swap16(value);
        }
        return value;
#endif
    }
    note_memory_fault(state, address);
    return 0;
}

inline std::uint32_t load32(State& state, std::uint32_t address) {
    if constexpr (cpu_profiling_compiled) {
        if (state.cpu_profile_enabled) {
            ++state.cpu_profile.memory_reads;
        }
    }
    const std::uint32_t canon = address & 0x1fffffffU;
    const std::uint32_t offset = canon - state.memory_base;
    if (state.memory != nullptr && offset <= state.memory_size - 4U && (address & 3U) == 0U) [[likely]] {
        std::uint32_t value{};
        std::memcpy(&value, state.memory + offset, sizeof(value));
        if constexpr (is_big_endian) {
            value = byte_swap(value);
        }
        return value;
    }
    if ((address & 3U) != 0U) {
        note_memory_fault(state, address);
        return 0;
    }
    if (const auto* pointer = mapped_address(state, address, 4)) {
        std::uint32_t value{};
        std::memcpy(&value, pointer, sizeof(value));
        if constexpr (is_big_endian) {
            value = byte_swap(value);
        }
        return value;
    }
    if (state.direct_memory_access) {
        if (!direct_address_ok(state, address, 4)) {
            return 0;
        }
#if defined(__PSP__)
        return *reinterpret_cast<volatile std::uint32_t*>(
            static_cast<std::uintptr_t>(address));
#else
        const auto* pointer = reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(address));
        std::uint32_t value{};
        std::memcpy(&value, pointer, sizeof(value));
        if constexpr (is_big_endian) {
            value = byte_swap(value);
        }
        return value;
#endif
    }
    note_memory_fault(state, address);
    return 0;
}

// Instruction immediates are read from the relocated guest image.  PSP PRX
// relocation records patch fields such as LUI/LO16/J26 after the module's load
// address is known, so baking the original ELF bits into generated C++ would
// produce pointers to address zero.
inline std::uint32_t instruction_word(const State& state,
                                      std::uint32_t current_pc,
                                      std::uint32_t fallback) {
    if ((current_pc & 3U) != 0U) {
        return fallback;
    }
    const auto* pointer = mapped_address(state, current_pc, 4);
    if (pointer == nullptr) {
        return fallback;
    }
    std::uint32_t value{};
    std::memcpy(&value, pointer, sizeof(value));
    if constexpr (is_big_endian) {
        value = byte_swap(value);
    }
    return value;
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
    if constexpr (cpu_profiling_compiled) {
        if (state.cpu_profile_enabled) {
            ++state.cpu_profile.memory_writes;
        }
    }
    const std::uint32_t canon = address & 0x1fffffffU;
    const std::uint32_t offset = canon - state.memory_base;
    if (state.memory != nullptr && offset < state.memory_size) [[likely]] {
        state.memory[offset] = value;
        return;
    }
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
    note_memory_fault(state, address);
}

inline void store16(State& state, std::uint32_t address, std::uint16_t value) {
    if constexpr (cpu_profiling_compiled) {
        if (state.cpu_profile_enabled) {
            ++state.cpu_profile.memory_writes;
        }
    }
    const std::uint32_t canon = address & 0x1fffffffU;
    const std::uint32_t offset = canon - state.memory_base;
    if (state.memory != nullptr && offset <= state.memory_size - 2U && (address & 1U) == 0U) [[likely]] {
        if constexpr (is_big_endian) {
            value = byte_swap16(value);
        }
        std::memcpy(state.memory + offset, &value, sizeof(value));
        return;
    }
    if ((address & 1U) != 0U) {
        note_memory_fault(state, address);
        return;
    }
    if constexpr (is_big_endian) {
        value = byte_swap16(value);
    }
    if (auto* pointer = mapped_address(state, address, 2)) {
        std::memcpy(pointer, &value, sizeof(value));
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
        auto* pointer = reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(address));
        std::memcpy(pointer, &value, sizeof(value));
#endif
        return;
    }
    note_memory_fault(state, address);
}

inline void store32(State& state, std::uint32_t address, std::uint32_t value) {
    if constexpr (cpu_profiling_compiled) {
        if (state.cpu_profile_enabled) {
            ++state.cpu_profile.memory_writes;
        }
    }
    const std::uint32_t canon = address & 0x1fffffffU;
    const std::uint32_t offset = canon - state.memory_base;
    if (state.memory != nullptr && offset <= state.memory_size - 4U && (address & 3U) == 0U) [[likely]] {
        if constexpr (is_big_endian) {
            value = byte_swap(value);
        }
        std::memcpy(state.memory + offset, &value, sizeof(value));
        return;
    }
    if ((address & 3U) != 0U) {
        note_memory_fault(state, address);
        return;
    }
    if constexpr (is_big_endian) {
        value = byte_swap(value);
    }
    if (auto* pointer = mapped_address(state, address, 4)) {
        std::memcpy(pointer, &value, sizeof(value));
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
        auto* pointer = reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(address));
        std::memcpy(pointer, &value, sizeof(value));
#endif
        return;
    }
    note_memory_fault(state, address);
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
    if constexpr (cpu_profiling_compiled) {
        if (state.cpu_profile_enabled) {
            ++state.cpu_profile.memory_reads;
        }
    }
    const auto offset = address & 3U;
    const auto aligned = address & ~3U;
    const auto count = offset + 1U;
    if constexpr (!is_big_endian) {
        if (const auto* pointer = mapped_address(state, aligned, count)) {
            std::memcpy(reinterpret_cast<std::uint8_t*>(&value) + (4U - count),
                        pointer, count);
            return value;
        }
        if (state.direct_memory_access) {
            if (!direct_address_ok(state, aligned, count)) {
                return value;
            }
            std::memcpy(reinterpret_cast<std::uint8_t*>(&value) + (4U - count),
                        reinterpret_cast<const void*>(
                            static_cast<std::uintptr_t>(aligned)),
                        count);
            return value;
        }
    } else {
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
    note_memory_fault(state, aligned);
    return value;
}

inline std::uint32_t load_word_right(State& state, std::uint32_t address,
                                     std::uint32_t value) {
    if constexpr (cpu_profiling_compiled) {
        if (state.cpu_profile_enabled) {
            ++state.cpu_profile.memory_reads;
        }
    }
    const auto offset = address & 3U;
    const auto count = 4U - offset;
    if constexpr (!is_big_endian) {
        if (const auto* pointer = mapped_address(state, address, count)) {
            std::memcpy(&value, pointer, count);
            return value;
        }
        if (state.direct_memory_access) {
            if (!direct_address_ok(state, address, count)) {
                return value;
            }
            std::memcpy(&value,
                        reinterpret_cast<const void*>(
                            static_cast<std::uintptr_t>(address)),
                        count);
            return value;
        }
    } else {
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
    note_memory_fault(state, address);
    return value;
}

inline void store_word_left(State& state, std::uint32_t address,
                            std::uint32_t value) {
    if constexpr (cpu_profiling_compiled) {
        if (state.cpu_profile_enabled) {
            ++state.cpu_profile.memory_writes;
        }
    }
    const auto offset = address & 3U;
    const auto aligned = address & ~3U;
    const auto count = offset + 1U;
    if constexpr (!is_big_endian) {
        if (auto* pointer = mapped_address(state, aligned, count)) {
            std::memcpy(pointer,
                        reinterpret_cast<const std::uint8_t*>(&value) +
                            (4U - count),
                        count);
            return;
        }
        if (state.direct_memory_access) {
            if (!direct_address_ok(state, aligned, count)) {
                return;
            }
            std::memcpy(reinterpret_cast<void*>(
                            static_cast<std::uintptr_t>(aligned)),
                        reinterpret_cast<const std::uint8_t*>(&value) +
                            (4U - count),
                        count);
            return;
        }
    } else {
        for (std::uint32_t i = 0; i <= offset; ++i) {
            const auto shift = 8U * (3U - offset + i);
            store8(state, aligned + i,
                   static_cast<std::uint8_t>(value >> shift));
        }
        return;
    }
    note_memory_fault(state, aligned);
}

inline void store_word_right(State& state, std::uint32_t address,
                             std::uint32_t value) {
    if constexpr (cpu_profiling_compiled) {
        if (state.cpu_profile_enabled) {
            ++state.cpu_profile.memory_writes;
        }
    }
    const auto offset = address & 3U;
    const auto count = 4U - offset;
    if constexpr (!is_big_endian) {
        if (auto* pointer = mapped_address(state, address, count)) {
            std::memcpy(pointer, &value, count);
            return;
        }
        if (state.direct_memory_access) {
            if (!direct_address_ok(state, address, count)) {
                return;
            }
            std::memcpy(reinterpret_cast<void*>(
                            static_cast<std::uintptr_t>(address)),
                        &value, count);
            return;
        }
    } else {
        const auto aligned = address & ~3U;
        for (std::uint32_t i = offset; i < 4U; ++i) {
            const auto shift = 8U * (i - offset);
            store8(state, aligned + i,
                   static_cast<std::uint8_t>(value >> shift));
        }
        return;
    }
    note_memory_fault(state, address);
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
    float result{};
    std::memcpy(&result, &state.fpr[index & 31U], sizeof(result));
    return result;
}

inline void set_f32(State& state, std::uint32_t index, float value) {
    std::memcpy(&state.fpr[index & 31U], &value, sizeof(value));
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

} // namespace psprecomp
