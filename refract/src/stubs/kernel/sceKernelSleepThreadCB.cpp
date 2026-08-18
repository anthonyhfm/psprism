#include "sleep_thread.hpp"

void sceKernelSleepThreadCB(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  sleep_guest_thread(implementation, state);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
