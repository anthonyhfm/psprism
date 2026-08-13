#include "sas_state.hpp"

void __sceSasSetADSR(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  const auto mask = state.gpr[6];
  const std::array rates{
      static_cast<std::int32_t>(state.gpr[7]),
      static_cast<std::int32_t>(state.gpr[8]),
      static_cast<std::int32_t>(state.gpr[9]),
      static_cast<std::int32_t>(state.gpr[10])};
  bool invalid = false;
  for (std::size_t index = 0; index < rates.size(); ++index) {
    if ((mask & (1U << index)) != 0U && rates[index] < 0) invalid = true;
  }
  if (invalid) {
    state.gpr[2] = sas_state::invalid_adsr;
  } else {
    const std::array<std::uint32_t, 4> unsigned_rates{
        state.gpr[7], state.gpr[8], state.gpr[9], state.gpr[10]};
    state.gpr[2] = sas_state::set_adsr_rates(
        state.gpr[4], static_cast<std::int32_t>(state.gpr[5]), mask,
        unsigned_rates);
  }
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
