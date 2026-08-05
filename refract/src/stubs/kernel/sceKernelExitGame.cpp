void sceKernelExitGame(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  implementation.exit_requested = true;
  host::request_frontend_exit();
  std::lock_guard lock(implementation.objects_mutex);
  for (const auto& [uid, semaphore] : implementation.semaphores) {
    static_cast<void>(uid);
    semaphore->changed.notify_all();
  }
  for (const auto& [uid, mutex] : implementation.mutexes) {
    static_cast<void>(uid);
    mutex->changed.notify_all();
  }
  for (const auto& [uid, event] : implementation.event_flags) {
    static_cast<void>(uid);
    event->changed.notify_all();
  }
  for (const auto& [uid, pool] : implementation.fixed_pools) {
    static_cast<void>(uid);
    pool->changed.notify_all();
  }
  for (const auto& [uid, pool] : implementation.variable_pools) {
    static_cast<void>(uid);
    pool->changed.notify_all();
  }
  state.gpr[2] = 0;
  state.stop_reason = psprecomp::StopReason::returned;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
