#include "atrac_state.hpp"

void sceAtracGetStreamDataInfo(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  static_cast<void>(implementation);
  const auto decoder = atrac_state::get(static_cast<int>(state.gpr[4]));
  if (decoder == nullptr) {
    state.gpr[2] = atrac_state::invalid_id;
    return;
  }
  std::lock_guard lock(decoder->decoder_mutex);
  std::uint32_t write_pointer{};
  std::uint32_t writable_bytes{};
  std::uint32_t read_offset{};
  decoder->stream_data_info(write_pointer, writable_bytes, read_offset);
  atrac_state::write_u32(state, state.gpr[5], write_pointer);
  atrac_state::write_u32(state, state.gpr[6], writable_bytes);
  atrac_state::write_u32(state, state.gpr[7], read_offset);
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
