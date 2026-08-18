#include "audio_state.hpp"

void sceAudioChRelease(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  static_cast<void>(implementation);
  state.gpr[2] = audio_state::release(state.gpr[4]) ? 0U : 0xffffffffU;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
