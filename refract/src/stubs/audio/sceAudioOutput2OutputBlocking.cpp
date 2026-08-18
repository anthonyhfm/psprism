#include "audio_state.hpp"

void sceAudioOutput2OutputBlocking(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  {
    GuestExecutionPause pause(implementation);
    state.gpr[2] = audio_state::output_output2(
        state, state.gpr[4], state.gpr[5], true);
  }
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
