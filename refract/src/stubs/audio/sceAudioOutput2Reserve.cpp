#include "audio_state.hpp"

void sceAudioOutput2Reserve(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  state.gpr[2] = audio_state::reserve_output2(state.gpr[4]);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
