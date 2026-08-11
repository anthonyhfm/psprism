#include "sas_state.hpp"

void __sceSasRevEVOL(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  if (state.gpr[5] > sas_state::max_volume ||
      state.gpr[6] > sas_state::max_volume) {
    state.gpr[2] = sas_state::invalid_effect_volume;
  } else {
    state.gpr[2] = sas_state::update_core(state.gpr[4],
                                          [](sas_state::Core&) { return 0U; });
  }
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
