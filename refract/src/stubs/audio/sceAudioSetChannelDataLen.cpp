#include "audio_state.hpp"

void sceAudioSetChannelDataLen(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  static_cast<void>(implementation);
  state.gpr[2] = audio_state::update(state.gpr[4], state.gpr[5], true, 0U, false)
                     ? 0U
                     : 0xffffffffU;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
