#include "thread_status.hpp"

void sceKernelDelayThread(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  thread_status::ScopedWait delay_status(current_thread_id, 2, 0);
  host::sleep_microseconds(state.gpr[4]);
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
