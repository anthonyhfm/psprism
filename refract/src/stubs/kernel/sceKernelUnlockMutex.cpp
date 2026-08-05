void sceKernelUnlockMutex(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::shared_ptr<Implementation::Mutex> mutex;
  {
    std::lock_guard lock(implementation.objects_mutex);
    const auto found =
        implementation.mutexes.find(static_cast<int>(state.gpr[4]));
    if (found != implementation.mutexes.end())
      mutex = found->second;
  }
  if (!mutex) {
    state.gpr[2] = unimplemented;
    return;
  }
  std::lock_guard lock(mutex->mutex);
  const auto count = static_cast<int>(state.gpr[5] ? state.gpr[5] : 1);
  mutex->lock_count = std::max(0, mutex->lock_count - count);
  if (mutex->lock_count == 0) {
    mutex->owner_thread_id = -1;
    mutex->changed.notify_all();
  }
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
