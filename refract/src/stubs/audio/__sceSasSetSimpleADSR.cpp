#include "sas_state.hpp"

void __sceSasSetSimpleADSR(Implementation& implementation,
                           psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  if (((state.gpr[7] >> 13U) & 1U) != 0U) {
    state.gpr[2] = sas_state::invalid_adsr_mode;
  } else {
    state.gpr[2] = sas_state::set_simple_adsr(
        state.gpr[4], static_cast<std::int32_t>(state.gpr[5]),
        state.gpr[6] & 0xffffU, state.gpr[7] & 0xffffU);
  }
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
