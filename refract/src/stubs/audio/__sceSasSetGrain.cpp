#include "sas_state.hpp"

void __sceSasSetGrain(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  state.gpr[2] = sas_state::set_grain(state.gpr[4], state.gpr[5]);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
