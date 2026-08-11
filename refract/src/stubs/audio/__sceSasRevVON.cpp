#include "sas_state.hpp"

void __sceSasRevVON(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  state.gpr[2] = sas_state::update_core(state.gpr[4],
                                        [](sas_state::Core&) { return 0U; });
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
