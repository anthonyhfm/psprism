#include "atrac_state.hpp"

void sceAtracDecodeData(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  static_cast<void>(implementation);
  bool was_decoded{};
  if (!atrac_state::mark_decoded(static_cast<int>(state.gpr[4]), was_decoded)) {
    state.gpr[2] = atrac_state::invalid_id;
    return;
  }
  const auto sample_count = was_decoded ? 0U : atrac_state::samples_per_frame;
  if (sample_count != 0U) {
    constexpr std::size_t channel_count = 2U;
    constexpr std::size_t sample_size = sizeof(std::int16_t);
    const auto byte_count = sample_count * channel_count * sample_size;
    if (auto* samples =
            psprecomp::mapped_address(state, state.gpr[5], byte_count))
      std::memset(samples, 0, byte_count);
  }
  atrac_state::write_u32(state, state.gpr[6], sample_count);
  atrac_state::write_u32(state, state.gpr[7], 1U);
  const auto remain_pointer =
      atrac_state::read_u32(state, state.gpr[29] + 0x10U);
  atrac_state::write_u32(state, remain_pointer, 0xffffffffU);
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
