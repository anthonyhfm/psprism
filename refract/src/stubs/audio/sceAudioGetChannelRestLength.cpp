#include "audio_state.hpp"

void sceAudioGetChannelRestLength(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  static_cast<void>(implementation);
  state.gpr[2] = audio_state::rest_length(state.gpr[4]);
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
