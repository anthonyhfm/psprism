#include "atrac_state.hpp"

void sceAtracSetLoopNum(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  static_cast<void>(implementation);
  const auto decoder = atrac_state::get(static_cast<int>(state.gpr[4]));
  if (decoder == nullptr) {
    state.gpr[2] = atrac_state::invalid_id;
    return;
  }
  std::lock_guard lock(decoder->decoder_mutex);
  if (decoder->track.loop_start == atrac_state::all_data_on_memory) {
    state.gpr[2] = atrac_state::no_loop_information;
    return;
  }
  decoder->loop_count = static_cast<int>(state.gpr[5]);
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
