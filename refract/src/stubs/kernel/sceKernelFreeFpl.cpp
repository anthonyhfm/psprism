void sceKernelFreeFpl(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::shared_ptr<Implementation::FixedPool> pool;
  {
    std::lock_guard lock(implementation.objects_mutex);
    const auto found =
        implementation.fixed_pools.find(static_cast<int>(state.gpr[4]));
    if (found == implementation.fixed_pools.end())
      return;
    pool = found->second;
  }
  {
    std::lock_guard lock(pool->mutex);
    pool->available.push_back(state.gpr[5]);
  }
  pool->changed.notify_one();
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
