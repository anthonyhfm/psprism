#include "ctrl_state.hpp"

void sceCtrlSetSamplingMode(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  state.gpr[2] = ctrl_state::set_sampling_mode(state.gpr[4]);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
