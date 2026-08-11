#include "sas_state.hpp"

void __sceSasGetOutputmode(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  state.gpr[2] = sas_state::output_mode(state.gpr[4]);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
