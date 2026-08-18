#include "atrac_state.hpp"

void sceAtracReleaseAtracID(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  static_cast<void>(implementation);
  state.gpr[2] = atrac_state::release(static_cast<int>(state.gpr[4]))
                     ? 0U
                     : atrac_state::invalid_id;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
