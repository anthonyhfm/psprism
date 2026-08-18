#include "sas_state.hpp"

void __sceSasCore(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  {
    GuestExecutionPause pause(implementation);
    state.gpr[2] = sas_state::mix(state, state.gpr[4], state.gpr[5], false);
  }
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
