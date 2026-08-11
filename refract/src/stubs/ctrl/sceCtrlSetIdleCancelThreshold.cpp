#include "ctrl_state.hpp"

void sceCtrlSetIdleCancelThreshold(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  state.gpr[2] = ctrl_state::set_idle_cancel_threshold(
      static_cast<std::int32_t>(state.gpr[4]),
      static_cast<std::int32_t>(state.gpr[5]));
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
