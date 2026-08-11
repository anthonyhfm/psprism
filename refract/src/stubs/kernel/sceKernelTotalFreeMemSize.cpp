#include "memory_state.hpp"

void sceKernelTotalFreeMemSize(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::lock_guard lock(implementation.objects_mutex);
  state.gpr[2] = memory_state::total_free_size(
      implementation.heap_cursor, implementation.stack_cursor,
      implementation.free_heap_blocks);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
