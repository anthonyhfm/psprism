#include "sas_state.hpp"

void __sceSasSetNoise(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  state.gpr[2] = sas_state::set_noise(
      state.gpr[4], static_cast<std::int32_t>(state.gpr[5]),
      static_cast<std::int32_t>(state.gpr[6]));
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
