void sceKernelAllocPartitionMemory(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::lock_guard lock(implementation.objects_mutex);
  const auto size = state.gpr[7];
  const auto address = implementation.allocate_heap(size);
  if (address == 0) {
    state.gpr[2] = out_of_memory;
    return;
  }
  const auto uid = implementation.allocate_uid();
  implementation.memory_blocks.emplace(
      uid, Implementation::MemoryBlock{address, size});
  state.gpr[2] = static_cast<std::uint32_t>(uid);
  if (implementation.verbose)
    std::fprintf(stderr,
                 "[psprism:memory] partition uid=%d size=%u address=%08x\n",
                 uid, size, address);
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
