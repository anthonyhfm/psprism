void sceKernelCreateFpl(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto block_size = state.gpr[7];
  const auto block_count = state.gpr[8];
  if (implementation.verbose)
    std::fprintf(stderr,
                 "[psprism:memory] create-fpl block_size=%u blocks=%u "
                 "sp=%08x\n",
                 block_size, block_count, state.gpr[29]);
  if (block_size == 0 || block_count == 0) {
    state.gpr[2] = out_of_memory;
    return;
  }
  auto pool = std::make_shared<Implementation::FixedPool>();
  std::lock_guard lock(implementation.objects_mutex);
  for (std::uint32_t index = 0; index < block_count; ++index) {
    const auto address = implementation.allocate_heap(block_size);
    if (address == 0) {
      for (const auto& block : pool->backing)
        implementation.free_heap(block.address, block.size);
      state.gpr[2] = out_of_memory;
      return;
    }
    pool->backing.push_back({address, block_size});
    pool->available.push_back(address);
  }
  const auto uid = implementation.allocate_uid();
  implementation.fixed_pools.emplace(uid, pool);
  state.gpr[2] = static_cast<std::uint32_t>(uid);
  if (implementation.verbose)
    std::fprintf(stderr, "[psprism:memory] fpl uid=%d\n", uid);
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
