void sceKernelCancelWakeupThread(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::shared_ptr<Implementation::GuestThread> thread;
  {
    std::lock_guard lock(implementation.objects_mutex);
    const auto found =
        implementation.threads.find(static_cast<int>(state.gpr[4]));
    if (found == implementation.threads.end()) {
      state.gpr[2] = 0x80020198U;
      return;
    }
    thread = found->second;
  }
  std::lock_guard lock(thread->sleep_mutex);
  state.gpr[2] = thread->wakeup_count;
  thread->wakeup_count = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
