void sceKernelDeleteFpl(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::lock_guard lock(implementation.objects_mutex);
  const auto found =
      implementation.fixed_pools.find(static_cast<int>(state.gpr[4]));
  if (found == implementation.fixed_pools.end()) {
    state.gpr[2] = unimplemented;
    return;
  }
  for (const auto& block : found->second->backing)
    implementation.free_heap(block.address, block.size);
  implementation.fixed_pools.erase(found);
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
