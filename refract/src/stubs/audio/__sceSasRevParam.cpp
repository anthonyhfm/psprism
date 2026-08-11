#include "sas_state.hpp"

void __sceSasRevParam(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  const auto delay = static_cast<std::int32_t>(state.gpr[5]);
  const auto feedback = static_cast<std::int32_t>(state.gpr[6]);
  if (delay < 0 || delay >= 128) {
    state.gpr[2] = sas_state::invalid_effect_delay;
  } else if (feedback < 0 || feedback >= 128) {
    state.gpr[2] = sas_state::invalid_effect_feedback;
  } else {
    state.gpr[2] = sas_state::update_core(state.gpr[4],
                                          [](sas_state::Core&) { return 0U; });
  }
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
