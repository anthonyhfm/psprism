#include "sas_state.hpp"

void __sceSasInit(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  state.gpr[2] = sas_state::initialize(state, state.gpr[4], state.gpr[5],
                                       state.gpr[6], state.gpr[7], state.gpr[8]);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
