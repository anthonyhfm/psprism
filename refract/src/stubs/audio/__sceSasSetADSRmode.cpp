#include "sas_state.hpp"

void __sceSasSetADSRmode(Implementation& implementation,
                         psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  const auto mask = state.gpr[6];
  const std::array modes{state.gpr[7], state.gpr[8], state.gpr[9],
                         state.gpr[10]};
  bool invalid = false;
  for (std::size_t index = 0; index < modes.size(); ++index) {
    if ((mask & (1U << index)) != 0U && modes[index] > 5U) invalid = true;
  }
  state.gpr[2] = invalid
                     ? sas_state::invalid_adsr_mode
                     : sas_state::validate_voice(
                           state.gpr[4],
                           static_cast<std::int32_t>(state.gpr[5]));
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
