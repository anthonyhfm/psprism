void sceKernelTryAllocateFpl(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::shared_ptr<Implementation::FixedPool> pool;
  {
    std::lock_guard lock(implementation.objects_mutex);
    const auto found =
        implementation.fixed_pools.find(static_cast<int>(state.gpr[4]));
    if (found == implementation.fixed_pools.end()) {
      state.gpr[2] = unimplemented;
      return;
    }
    pool = found->second;
  }
  std::uint32_t address{};
  {
    std::lock_guard lock(pool->mutex);
    if (!pool->available.empty()) {
      address = pool->available.back();
      pool->available.pop_back();
    }
  }
  if (address == 0U) {
    state.gpr[2] = out_of_memory;
    return;
  }
  if (auto* output = guest_pointer<std::uint32_t>(state, state.gpr[5]))
    *output = address;
  state.gpr[2] = 0;
  if (implementation.verbose)
    std::fprintf(stderr,
                 "[psprism:memory] try-allocate-fpl uid=%u output=%08x "
                 "address=%08x\n",
                 state.gpr[4], state.gpr[5], address);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
