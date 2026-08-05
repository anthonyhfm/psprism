#include "audio_state.hpp"

void sceAudioOutputPanned(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  static_cast<void>(implementation);
  state.gpr[2] = audio_state::output(state.gpr[4], false);
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
