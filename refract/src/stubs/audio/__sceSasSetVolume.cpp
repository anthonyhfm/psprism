#include "sas_state.hpp"

void __sceSasSetVolume(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  state.gpr[2] = sas_state::set_volume(
      state.gpr[4], static_cast<std::int32_t>(state.gpr[5]),
      static_cast<std::int32_t>(state.gpr[6]),
      static_cast<std::int32_t>(state.gpr[7]),
      static_cast<std::int32_t>(state.gpr[8]),
      static_cast<std::int32_t>(state.gpr[9]));
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
