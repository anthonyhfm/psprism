void sceKernelSignalSema(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::shared_ptr<Implementation::Semaphore> semaphore;
  {
    std::lock_guard lock(implementation.objects_mutex);
    const auto found =
        implementation.semaphores.find(static_cast<int>(state.gpr[4]));
    if (found == implementation.semaphores.end())
      return;
    semaphore = found->second;
  }
  {
    std::lock_guard lock(semaphore->mutex);
    semaphore->count =
        std::min(semaphore->maximum,
                 semaphore->count + static_cast<int>(state.gpr[5]));
    static thread_local std::uint32_t logged_signals{};
    if (implementation.verbose &&
        (logged_signals < 16U || (logged_signals & 0xffffU) == 0U)) {
      std::fprintf(stderr,
                   "[psprism:sema] signal thread=%d uid=%u add=%u count=%d\n",
                   current_thread_id, state.gpr[4], state.gpr[5],
                   semaphore->count);
    }
    ++logged_signals;
  }
  semaphore->changed.notify_all();
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
