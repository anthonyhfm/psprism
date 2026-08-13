#include "audio_state.hpp"

void sceAudioOutput2Release(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  state.gpr[2] = audio_state::release_output2();
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
