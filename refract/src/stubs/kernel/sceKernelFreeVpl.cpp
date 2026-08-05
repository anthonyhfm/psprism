void sceKernelFreeVpl(Implementation& implementation, psprecomp::State& state) {
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
  {
    std::lock_guard lock(pool->mutex);
    const auto allocation = pool->allocated.find(state.gpr[5]);
    if (allocation == pool->allocated.end())
      return;
    pool->available.push_back({allocation->first, allocation->second});
    pool->allocated.erase(allocation);
    std::sort(pool->available.begin(), pool->available.end(),
              [](const Implementation::MemoryBlock& left,
                 const Implementation::MemoryBlock& right) {
                return left.address < right.address;
              });
    for (std::size_t i = 1; i < pool->available.size();) {
      auto& previous = pool->available[i - 1U];
      const auto& current = pool->available[i];
      if (previous.address + previous.size == current.address) {
        previous.size += current.size;
        pool->available.erase(pool->available.begin() + i);
      } else {
        ++i;
      }
    }
  }
  pool->changed.notify_one();
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
