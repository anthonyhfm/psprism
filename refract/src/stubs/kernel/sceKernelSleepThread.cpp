#include "sleep_thread.hpp"

void sceKernelSleepThread(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  sleep_guest_thread(implementation, state);
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
