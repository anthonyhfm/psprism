#include "sas_state.hpp"

void __sceSasCoreWithMix(Implementation& implementation,
                         psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  if (!sas_state::volume_valid(static_cast<std::int32_t>(state.gpr[6])) ||
      !sas_state::volume_valid(static_cast<std::int32_t>(state.gpr[7]))) {
    state.gpr[2] = sas_state::invalid_volume;
  } else {
    GuestExecutionPause pause(implementation);
    state.gpr[2] = sas_state::mix(
        state, state.gpr[4], state.gpr[5], true,
        static_cast<std::int32_t>(state.gpr[6]),
        static_cast<std::int32_t>(state.gpr[7]));
  }
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
