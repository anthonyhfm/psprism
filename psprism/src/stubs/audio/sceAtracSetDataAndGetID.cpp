#include "atrac_state.hpp"

void sceAtracSetDataAndGetID(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  static_cast<void>(implementation);
  state.gpr[2] = static_cast<std::uint32_t>(
      atrac_state::create(state.gpr[4], state.gpr[5]));
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
