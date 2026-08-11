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
  static thread_local std::uint32_t logged_polls{};
  const auto log_poll = implementation.verbose &&
                        (logged_polls < 8U ||
                         (logged_polls & 0xffffU) == 0U);
  ++logged_polls;
  if (requested <= 0 || requested > semaphore->count) {
    if (log_poll)
      std::fprintf(stderr,
                   "[psprism:sema] poll thread=%d uid=%u name=%s "
                   "request=%d count=%d result=empty\n",
                   current_thread_id, state.gpr[4], semaphore->name.c_str(),
                   requested, semaphore->count);
    state.gpr[2] = semaphore_zero;
    return;
  }
  semaphore->count -= requested;
  if (log_poll)
    std::fprintf(stderr,
                 "[psprism:sema] poll thread=%d uid=%u name=%s request=%d "
                 "count=%d result=acquired\n",
                 current_thread_id, state.gpr[4], semaphore->name.c_str(),
                 requested, semaphore->count);
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
