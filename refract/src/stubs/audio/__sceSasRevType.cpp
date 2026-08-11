#include "sas_state.hpp"

void __sceSasRevType(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  const auto type = static_cast<std::int32_t>(state.gpr[5]);
  if (type < -1 || type > 8) {
    state.gpr[2] = sas_state::invalid_effect_type;
  } else {
    state.gpr[2] = sas_state::update_core(state.gpr[4],
                                          [](sas_state::Core&) { return 0U; });
  }
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
