#include "display_state.hpp"

#if !defined(__PSP__)
void wait_for_next_vblank(Implementation& implementation) {
  const auto delay = display_state::microseconds_until_next_vblank(
      implementation.elapsed_microseconds());
  GuestExecutionPause pause(implementation);
  std::unique_lock lock(implementation.exit_mutex);
  implementation.exit_changed.wait_for(
      lock, std::chrono::microseconds(delay),
      [&] { return implementation.exit_requested.load(); });
}
#endif

void sceDisplayWaitVblank(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  wait_for_next_vblank(implementation);
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
