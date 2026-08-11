#include "atrac_state.hpp"

void sceAtracSetData(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  static_cast<void>(implementation);
  state.gpr[2] = atrac_state::set_data(
      state, static_cast<int>(state.gpr[4]), state.gpr[5], state.gpr[6]);
  if (implementation.verbose) {
    const auto decoder = atrac_state::get(static_cast<int>(state.gpr[4]));
    if (decoder != nullptr && state.gpr[2] == atrac_state::success) {
      std::lock_guard lock(decoder->decoder_mutex);
      std::fprintf(stderr,
                   "[psprism:atrac] data id=%d codec=%08x bytes=%u "
                   "block=%u channels=%u\n",
                   static_cast<int>(state.gpr[4]), decoder->track.codec_type,
                   state.gpr[6], decoder->track.block_align,
                   decoder->track.channels);
    } else {
      std::fprintf(stderr,
                   "[psprism:atrac] data id=%d bytes=%u result=%08x\n",
                   static_cast<int>(state.gpr[4]), state.gpr[6], state.gpr[2]);
    }
  }
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
