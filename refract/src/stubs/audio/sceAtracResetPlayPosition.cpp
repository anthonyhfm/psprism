#include "atrac_state.hpp"

void sceAtracResetPlayPosition(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  static_cast<void>(implementation);
  const auto decoder = atrac_state::get(static_cast<int>(state.gpr[4]));
  if (decoder == nullptr) {
    state.gpr[2] = atrac_state::invalid_id;
    return;
  }
  std::lock_guard lock(decoder->decoder_mutex);
  if (state.gpr[5] > decoder->track.end_sample) {
    state.gpr[2] = atrac_state::bad_sample;
    return;
  }
  if (!decoder->seek(state.gpr[5])) {
    state.gpr[2] = atrac_state::bad_data;
    return;
  }
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
