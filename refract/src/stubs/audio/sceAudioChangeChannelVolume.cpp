#include "audio_state.hpp"

void sceAudioChangeChannelVolume(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  static_cast<void>(implementation);
  state.gpr[2] = audio_state::sample_count(state.gpr[4]) != 0U
                     ? 0U
                     : 0xffffffffU;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
