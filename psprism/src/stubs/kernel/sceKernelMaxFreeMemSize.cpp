void sceKernelMaxFreeMemSize(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::lock_guard lock(implementation.objects_mutex);
  auto largest =
      implementation.stack_cursor > implementation.heap_cursor
          ? implementation.stack_cursor - implementation.heap_cursor
          : 0U;
  for (const auto& block : implementation.free_heap_blocks)
    largest = std::max(largest, block.size);
  state.gpr[2] = largest;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
