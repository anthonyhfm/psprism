#include "atrac_state.hpp"

void sceAtracGetStreamDataInfo(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  static_cast<void>(implementation);
  atrac_state::Decoder decoder;
  if (!atrac_state::get(static_cast<int>(state.gpr[4]), decoder)) {
    state.gpr[2] = atrac_state::invalid_id;
    return;
  }
  atrac_state::write_u32(state, state.gpr[5], decoder.buffer);
  atrac_state::write_u32(state, state.gpr[6], 0U);
  atrac_state::write_u32(state, state.gpr[7], decoder.buffer_size);
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
