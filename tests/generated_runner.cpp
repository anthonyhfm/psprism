#include <psprecomp/runtime.hpp>

#include <cstdint>
#include <iostream>

extern "C" unsigned recomp_test(unsigned a, unsigned b);

namespace psprecomp::generated {
void run(State&, std::uint32_t return_address, std::uint64_t max_steps);

// Project-mode dispatch normally resolves PSP imports in imports.cpp.  The
// arithmetic fixture has no imports, so its native runner provides the empty
// bridge and can exercise the fast generated shard directly.
bool dispatch_import(State&, std::uint32_t) {
    return false;
}
}

int main() {
#ifndef PSPRECOMP_TEST_ENTRY
#define PSPRECOMP_TEST_ENTRY 0x08804000U
#endif
    constexpr std::uint32_t entry = PSPRECOMP_TEST_ENTRY;
    constexpr std::uint32_t return_address = 0xfffffff0U;
    constexpr std::uint32_t inputs[][2] = {
        {0, 0}, {1, 2}, {0xffffffffU, 0x12345678U},
        {0xdeadbeefU, 0xc001d00dU}, {42, 9001},
    };

    for (const auto& input : inputs) {
        psprecomp::State state;
        state.pc = entry;
        state.gpr[4] = input[0];
        state.gpr[5] = input[1];
        state.gpr[31] = return_address;
        psprecomp::generated::run(state, return_address, 1000);
        const auto expected = recomp_test(input[0], input[1]);
        if (state.stop_reason != psprecomp::StopReason::returned ||
            state.gpr[2] != expected) {
            std::cerr << "roundtrip mismatch: got " << state.gpr[2]
                      << ", expected " << expected << '\n';
            return 1;
        }
    }
    return 0;
}
