#include <pspkernel.h>

#include <psprecomp/runtime.hpp>

PSP_MODULE_INFO("PSPRecomp Roundtrip", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

namespace psprecomp::generated {
void run(State&, std::uint32_t return_address, std::uint64_t max_steps);
}

int main() {
    constexpr std::uint32_t return_address = 0xfffffff0U;
    psprecomp::State state;
    state.pc = 0x08804000U;
    state.gpr[4] = 1;
    state.gpr[5] = 2;
    state.gpr[31] = return_address;
    psprecomp::generated::run(state, return_address, 1000);
    const bool success = state.stop_reason == psprecomp::StopReason::returned &&
                         state.gpr[2] == 0xf0c9e6c6U;
    sceKernelExitGame();
    return success ? 0 : 1;
}
