#include "sas_state.hpp"

void __sceSasGetAllEnvelopeHeights(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  state.gpr[2] =
      sas_state::all_envelope_heights(state, state.gpr[4], state.gpr[5]);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
