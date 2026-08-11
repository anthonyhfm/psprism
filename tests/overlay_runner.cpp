#include <psprecomp/runtime.hpp>

#include <array>
#include <bit>
#include <cstdint>

#ifndef PSPRECOMP_OVERLAY_FUNCTION
#error PSPRECOMP_OVERLAY_FUNCTION must name the generated overlay runner
#endif

#ifndef PSPRECOMP_OVERLAY_START
#error PSPRECOMP_OVERLAY_START must contain the Guest function offset
#endif

#ifndef PSPRECOMP_OVERLAY_CALLEE
#error PSPRECOMP_OVERLAY_CALLEE must contain the unchanged Guest callee offset
#endif

#ifndef PSPRECOMP_OVERLAY_RESUME
#error PSPRECOMP_OVERLAY_RESUME must contain the post-call Guest offset
#endif

namespace psprecomp::generated {
bool PSPRECOMP_OVERLAY_FUNCTION(State& state, std::uint32_t entry_pc);
}

int main() {
    constexpr std::uint32_t base = 0x08800000U;
    constexpr std::uint32_t return_address = 0x08900000U;
    std::array<std::uint8_t, 0x1000> memory{};
    psprecomp::State state;
    state.memory = memory.data();
    state.memory_size = static_cast<std::uint32_t>(memory.size());
    state.memory_base = base;
    state.pc = base + PSPRECOMP_OVERLAY_START;
    state.gpr[4] = 8U;
    state.gpr[29] = base + 0x800U;
    state.gpr[31] = return_address;
    const auto relocated_jal =
        0x0c000000U |
        (((base + PSPRECOMP_OVERLAY_CALLEE) >> 2U) & 0x03ffffffU);
    psprecomp::store32(state, base + PSPRECOMP_OVERLAY_RESUME - 8U,
                       relocated_jal);
    if (!psprecomp::generated::PSPRECOMP_OVERLAY_FUNCTION(state, state.pc)) {
        return 1;
    }
    if (state.stop_reason != psprecomp::StopReason::running ||
        state.pc != base + PSPRECOMP_OVERLAY_CALLEE ||
        state.gpr[31] != base + PSPRECOMP_OVERLAY_RESUME ||
        state.fpr[12] != std::bit_cast<std::uint32_t>(8.0F) ||
        state.gpr[29] != base + 0x7f8U || state.branch_pending) {
        return 2;
    }

    // Model execution of the unchanged original Allegrex callee. The second
    // runner entry is the same continuation used by the PSP trampoline.
    state.fpr[0] = std::bit_cast<std::uint32_t>(24.0F);
    state.pc = base + PSPRECOMP_OVERLAY_RESUME;
    if (!psprecomp::generated::PSPRECOMP_OVERLAY_FUNCTION(state, state.pc)) {
        return 3;
    }
    if (state.stop_reason != psprecomp::StopReason::running ||
        state.gpr[2] != 124U || state.pc != return_address ||
        state.gpr[29] != base + 0x800U ||
        state.branch_pending) {
        return 4;
    }
    return 0;
}
