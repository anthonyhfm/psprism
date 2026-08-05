#include "audio_state.hpp"

void sceAudioChReserve(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  static_cast<void>(implementation);
  state.gpr[2] = static_cast<std::uint32_t>(audio_state::reserve(
      static_cast<std::int32_t>(state.gpr[4]), state.gpr[5], state.gpr[6]));
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
