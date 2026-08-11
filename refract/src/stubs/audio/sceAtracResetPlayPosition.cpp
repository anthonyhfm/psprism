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
  const auto frame = decoder->track.samples_per_frame() == 0U
                         ? 0U
                         : state.gpr[5] / decoder->track.samples_per_frame();
  const auto offset = static_cast<std::uint64_t>(decoder->track.data_offset) +
                      static_cast<std::uint64_t>(frame) *
                          decoder->track.block_align;
  if (offset >= decoder->encoded.size()) {
    state.gpr[2] = atrac_state::bad_data;
    return;
  }
  decoder->rewind();
  decoder->data_cursor = static_cast<std::uint32_t>(offset);
  decoder->decoded_samples = state.gpr[5];
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
