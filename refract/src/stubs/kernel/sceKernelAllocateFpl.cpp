void sceKernelAllocateFpl(Implementation& implementation, psprecomp::State& state) {
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
  std::unique_lock lock(pool->mutex);
  pool->changed.wait(lock, [&] {
    return !pool->available.empty() || implementation.exit_requested;
  });
  if (pool->available.empty())
    return;
  const auto address = pool->available.back();
  pool->available.pop_back();
  if (auto* output = guest_pointer<std::uint32_t>(state, state.gpr[5]))
    *output = address;
  state.gpr[2] = 0;
  if (implementation.verbose)
    std::fprintf(stderr,
                 "[psprism:memory] allocate-fpl uid=%u output=%08x "
                 "address=%08x\n",
                 state.gpr[4], state.gpr[5], address);
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
