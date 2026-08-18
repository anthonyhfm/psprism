#include "atrac_state.hpp"

void sceAtracAddStreamData(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  static_cast<void>(implementation);
  const auto decoder = atrac_state::get(static_cast<int>(state.gpr[4]));
  if (decoder == nullptr) {
    state.gpr[2] = atrac_state::invalid_id;
    return;
  }
  std::lock_guard lock(decoder->decoder_mutex);
  state.gpr[2] = decoder->add_stream_data(state, state.gpr[5]);
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
