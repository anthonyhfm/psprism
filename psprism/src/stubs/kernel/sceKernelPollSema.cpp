void sceKernelPollSema(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::shared_ptr<Implementation::Semaphore> semaphore;
  {
    std::lock_guard lock(implementation.objects_mutex);
    const auto found =
        implementation.semaphores.find(static_cast<int>(state.gpr[4]));
    if (found == implementation.semaphores.end()) {
      state.gpr[2] = unimplemented;
      return;
    }
    semaphore = found->second;
  }
  const auto requested = static_cast<int>(state.gpr[5]);
  std::lock_guard lock(semaphore->mutex);
  if (requested <= 0 || requested > semaphore->count) {
    state.gpr[2] = wait_timeout;
    return;
  }
  semaphore->count -= requested;
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
