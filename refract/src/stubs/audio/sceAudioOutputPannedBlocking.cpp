#include "audio_state.hpp"

void sceAudioOutputPannedBlocking(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  {
    GuestExecutionPause pause(implementation);
    state.gpr[2] = audio_state::output(state.gpr[4], true);
  }
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
