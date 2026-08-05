#include "atrac_state.hpp"

void sceAtracAddStreamData(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  static_cast<void>(implementation);
  atrac_state::Decoder decoder;
  state.gpr[2] = atrac_state::get(static_cast<int>(state.gpr[4]), decoder)
                     ? 0U
                     : atrac_state::invalid_id;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
