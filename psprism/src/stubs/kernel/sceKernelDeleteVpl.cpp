void sceKernelDeleteVpl(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::lock_guard lock(implementation.objects_mutex);
  const auto found = implementation.variable_pools.find(
      static_cast<int>(state.gpr[4]));
  if (found == implementation.variable_pools.end())
    return;
  implementation.free_heap(found->second->backing.address,
                             found->second->backing.size);
  implementation.variable_pools.erase(found);
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
