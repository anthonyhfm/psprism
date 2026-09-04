#include "generated.hpp"

#include <psprecomp/patch.hpp>

#include <array>
#include <cstdint>
#include <iostream>

extern "C" unsigned recomp_test(unsigned a, unsigned b);

namespace psprecomp::platform {
bool dispatch_import(State&, std::uint32_t) { return false; }
} // namespace psprecomp::platform

namespace {

DEFINE_GAME_FUNCTION(original_recomp_test, psprecomp::patch::image_offset(0),
                     std::uint32_t, std::uint32_t, std::uint32_t);
DEFINE_GAME_GLOBAL(patch_observed_value,
                   psprecomp::patch::absolute_address(0x08400000U),
                   std::uint32_t);

std::uint32_t patched_recomp_test(std::uint32_t a, std::uint32_t b) {
    patch_observed_value = a + b;
    return original_recomp_test.original(a, b) + 1U;
}

RECOMP_PATCH_FUNCTION_BY_NAME("recomp_test", patched_recomp_test);

} // namespace

int main() {
    constexpr std::uint32_t return_address = 0xfffffff0U;
    constexpr std::uint32_t inputs[][2] = {
        {0U, 0U},
        {1U, 2U},
        {0xdeadbeefU, 0xc001d00dU},
    };

    for (const auto& input : inputs) {
        std::array<std::uint8_t, 64> volatile_memory{};
        psprecomp::State state;
        state.volatile_memory = volatile_memory.data();
        state.volatile_memory_size = volatile_memory.size();
        state.pc = psprecomp::generated::guest_entry;
        state.gpr[4] = input[0];
        state.gpr[5] = input[1];
        state.gpr[31] = return_address;
        psprecomp::generated::run(state, return_address, 1000);

        const auto expected = recomp_test(input[0], input[1]) + 1U;
        const auto observed = psprecomp::patch::read_guest<std::uint32_t>(
            state, 0x08400000U);
        if (state.stop_reason != psprecomp::StopReason::returned ||
            state.gpr[2] != expected || observed != input[0] + input[1]) {
            std::cerr << "patch roundtrip mismatch: got " << state.gpr[2]
                      << ", expected " << expected << ", global " << observed
                      << '\n';
            return 1;
        }
    }
    return 0;
}
