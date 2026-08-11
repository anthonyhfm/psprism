#include "audio_state.hpp"

void sceAudioOutputPannedBlocking(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  {
    GuestExecutionPause pause(implementation);
    state.gpr[2] = audio_state::output(state, state.gpr[4], state.gpr[5],
                                       state.gpr[6], state.gpr[7], true);
  }
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
