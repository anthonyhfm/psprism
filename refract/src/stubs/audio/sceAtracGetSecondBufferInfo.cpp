#include "atrac_state.hpp"

void sceAtracGetSecondBufferInfo(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  static_cast<void>(implementation);
  const auto decoder = atrac_state::get(static_cast<int>(state.gpr[4]));
  if (decoder == nullptr) {
    state.gpr[2] = atrac_state::invalid_id;
    return;
  }
  atrac_state::write_u32(state, state.gpr[5], 0U);
  atrac_state::write_u32(state, state.gpr[6], 0U);
  state.gpr[2] = atrac_state::second_buffer_not_needed;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
