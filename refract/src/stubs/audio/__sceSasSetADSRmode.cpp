#include "sas_state.hpp"

void __sceSasSetADSRmode(Implementation& implementation,
                         psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  const auto mask = state.gpr[6];
  const std::array modes{state.gpr[7], state.gpr[8], state.gpr[9],
                         state.gpr[10]};
  const auto attack = modes[0] & 0x7fffffffU;
  const auto decay = modes[1] & 0x7fffffffU;
  const auto sustain = modes[2] & 0x7fffffffU;
  const auto release = modes[3] & 0x7fffffffU;
  const auto invalid_mask =
      (attack > 5U || (attack & 1U) != 0U ? 1U : 0U) |
      (decay > 5U || (decay & 1U) != 1U ? 2U : 0U) |
      (sustain > 5U ? 4U : 0U) |
      (release > 5U || (release & 1U) != 1U ? 8U : 0U);
  if ((invalid_mask & mask) != 0U) {
    state.gpr[2] = sas_state::invalid_adsr_mode;
  } else {
    state.gpr[2] = sas_state::set_adsr_curves(
        state.gpr[4], static_cast<std::int32_t>(state.gpr[5]), mask,
        std::array<std::uint32_t, 4>{attack, decay, sustain, release});
  }
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
