void sceKernelCreateSema(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  auto semaphore = std::make_shared<Implementation::Semaphore>();
  if (const auto* name = guest_string(state, state.gpr[4]))
    semaphore->name = name;
  semaphore->count = static_cast<int>(state.gpr[6]);
  semaphore->maximum = static_cast<int>(state.gpr[7]);
  std::lock_guard lock(implementation.objects_mutex);
  const auto uid = implementation.allocate_uid();
  implementation.semaphores.emplace(uid, semaphore);
  if (implementation.verbose) {
    std::fprintf(stderr,
                 "[psprism:sema] create uid=%d name=%s count=%d max=%d\n",
                 uid, semaphore->name.c_str(), semaphore->count,
                 semaphore->maximum);
  }
  state.gpr[2] = static_cast<std::uint32_t>(uid);
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
