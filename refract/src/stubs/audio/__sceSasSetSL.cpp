#include "sas_state.hpp"

void __sceSasSetSL(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  if (state.gpr[6] > 0x40000000U) {
    state.gpr[2] = sas_state::invalid_adsr;
  } else {
    state.gpr[2] = sas_state::update_voice(
        state.gpr[4], static_cast<std::int32_t>(state.gpr[5]),
        [&](sas_state::Voice& voice) {
          voice.envelope_height = state.gpr[6];
          return 0U;
        });
  }
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
