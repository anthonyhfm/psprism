#include "audio_state.hpp"

void sceAudioOutput2GetRestSample(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  state.gpr[2] = audio_state::output2_rest_length();
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
