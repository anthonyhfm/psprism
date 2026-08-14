#include "atrac_state.hpp"

void sceAtracSetDataAndGetID(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  static_cast<void>(implementation);
  const auto* source = psprecomp::mapped_address(state, state.gpr[4], state.gpr[5]);
  atrac_state::Track track;
  if (source == nullptr || !atrac_state::parse_riff(source, state.gpr[5], track)) {
    state.gpr[2] = atrac_state::unknown_format;
    return;
  }
  const auto id = atrac_state::create(track.codec_type);
  if (id < 0) {
    state.gpr[2] = static_cast<std::uint32_t>(id);
    return;
  }
  const auto result = atrac_state::set_data(state, id, state.gpr[4], state.gpr[5]);
  if (result != atrac_state::success) {
    atrac_state::release(id);
    state.gpr[2] = result;
    return;
  }
  state.gpr[2] = static_cast<std::uint32_t>(id);
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
