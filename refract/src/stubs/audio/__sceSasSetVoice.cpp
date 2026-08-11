#include "sas_state.hpp"

void __sceSasSetVoice(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  state.gpr[2] = sas_state::set_voice(
      state.gpr[4], static_cast<std::int32_t>(state.gpr[5]), state,
      state.gpr[6], static_cast<std::int32_t>(state.gpr[7]),
      static_cast<std::int32_t>(state.gpr[8]));
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
