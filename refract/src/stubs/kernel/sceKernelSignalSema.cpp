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
  }
  semaphore->changed.notify_all();
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
