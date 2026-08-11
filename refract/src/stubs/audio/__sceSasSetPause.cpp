#include "sas_state.hpp"

void __sceSasSetPause(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  state.gpr[2] =
      sas_state::set_pause(state.gpr[4], state.gpr[5], state.gpr[6] != 0U);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
