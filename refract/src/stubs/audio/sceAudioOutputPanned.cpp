#include "audio_state.hpp"

void sceAudioOutputPanned(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  static_cast<void>(implementation);
  state.gpr[2] = audio_state::output(
      state, state.gpr[4], static_cast<std::int32_t>(state.gpr[5]),
      static_cast<std::int32_t>(state.gpr[6]), state.gpr[7], false);
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
