#include "atrac_state.hpp"

void sceAtracGetLoopStatus(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  static_cast<void>(implementation);
  const auto decoder = atrac_state::get(static_cast<int>(state.gpr[4]));
  if (decoder == nullptr) {
    state.gpr[2] = atrac_state::invalid_id;
    return;
  }
  std::lock_guard lock(decoder->decoder_mutex);
  atrac_state::write_u32(state, state.gpr[5],
                         static_cast<std::uint32_t>(decoder->loop_count));
  atrac_state::write_u32(
      state, state.gpr[6],
      decoder->track.loop_start == atrac_state::all_data_on_memory ? 0U : 1U);
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
