#include "sas_state.hpp"

void __sceSasGetEnvelopeHeight(Implementation& implementation,
                               psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  state.gpr[2] = sas_state::envelope_height(
      state.gpr[4], static_cast<std::int32_t>(state.gpr[5]));
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
