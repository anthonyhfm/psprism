#include "audio_state.hpp"

void sceAudioChangeChannelVolume(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  static_cast<void>(implementation);
  state.gpr[2] = audio_state::change_volume(
      state.gpr[4], state.gpr[5], state.gpr[6]);
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
