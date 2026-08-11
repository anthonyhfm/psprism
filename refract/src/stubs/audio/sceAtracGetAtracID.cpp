#include "atrac_state.hpp"

void sceAtracGetAtracID(Implementation& implementation,
                        psprecomp::State& state) {
#if !defined(__PSP__)
  static_cast<void>(implementation);
  state.gpr[2] = static_cast<std::uint32_t>(atrac_state::create(state.gpr[4]));
  if (implementation.verbose)
    std::fprintf(stderr, "[psprism:atrac] create id=%d codec=%08x\n",
                 static_cast<int>(state.gpr[2]), state.gpr[4]);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
