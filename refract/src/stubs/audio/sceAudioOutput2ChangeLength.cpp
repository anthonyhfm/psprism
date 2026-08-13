#include "audio_state.hpp"

void sceAudioOutput2ChangeLength(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  state.gpr[2] = audio_state::change_output2_length(state.gpr[4]);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
