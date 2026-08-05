void sceKernelAllocateVplCB(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::shared_ptr<Implementation::VariablePool> pool;
  {
    std::lock_guard lock(implementation.objects_mutex);
    const auto found =
        implementation.variable_pools.find(static_cast<int>(state.gpr[4]));
    if (found == implementation.variable_pools.end())
      return;
    pool = found->second;
  }
  const auto requested = align_up(state.gpr[5], 8U);
  std::unique_lock lock(pool->mutex);
  const auto allocate = [&]() -> std::uint32_t {
    for (auto i = pool->available.begin(); i != pool->available.end(); ++i) {
      if (i->size < requested)
        continue;
      const auto address = i->address;
      i->address += requested;
      i->size -= requested;
      if (i->size == 0)
        pool->available.erase(i);
      pool->allocated.emplace(address, requested);
      return address;
    }
    return 0;
  };
  auto address = allocate();
  while (address == 0 && !implementation.exit_requested) {
    pool->changed.wait(lock);
    address = allocate();
  }
  if (address == 0) {
    state.gpr[2] = out_of_memory;
    return;
  }
  if (auto* output = guest_pointer<std::uint32_t>(state, state.gpr[6]))
    *output = address;
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
