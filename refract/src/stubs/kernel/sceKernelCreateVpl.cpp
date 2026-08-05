void sceKernelCreateVpl(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto size = state.gpr[7];
  if (size == 0) {
    state.gpr[2] = out_of_memory;
    return;
  }
  auto pool = std::make_shared<Implementation::VariablePool>();
  std::lock_guard lock(implementation.objects_mutex);
  const auto address = implementation.allocate_heap(size, 8U);
  if (address == 0) {
    state.gpr[2] = out_of_memory;
    return;
  }
  pool->available.push_back({address, size});
  pool->backing = {address, size};
  const auto uid = implementation.allocate_uid();
  implementation.variable_pools.emplace(uid, pool);
  state.gpr[2] = static_cast<std::uint32_t>(uid);
  if (implementation.verbose)
    std::fprintf(stderr,
                 "[psprism:memory] vpl uid=%d size=%u address=%08x\n", uid,
                 size, address);
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
