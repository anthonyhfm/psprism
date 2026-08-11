void sceKernelFreePartitionMemory(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::lock_guard lock(implementation.objects_mutex);
  const auto found =
      implementation.memory_blocks.find(static_cast<int>(state.gpr[4]));
  if (found == implementation.memory_blocks.end()) {
    state.gpr[2] = unimplemented;
    return;
  }
  implementation.free_heap(found->second.address, found->second.size);
  implementation.memory_blocks.erase(found);
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
