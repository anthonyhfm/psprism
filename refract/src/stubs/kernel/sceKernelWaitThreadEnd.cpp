void sceKernelWaitThreadEnd(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::shared_ptr<Implementation::GuestThread> thread;
  {
    std::lock_guard lock(implementation.objects_mutex);
    const auto found =
        implementation.threads.find(static_cast<int>(state.gpr[4]));
    if (found == implementation.threads.end())
      return;
    thread = found->second;
  }
  if (implementation.verbose) {
    std::fprintf(stderr,
                 "[psprism:thread] wait-end caller=%d uid=%d name=%s "
                 "finished=%u joinable=%u timeout=%08x\n",
                 current_thread_id, thread->uid, thread->name.c_str(),
                 thread->finished.load() ? 1U : 0U,
                 thread->host_thread.joinable() ? 1U : 0U, state.gpr[5]);
  }
  if (thread->host_thread.joinable() && thread->uid != current_thread_id) {
    GuestExecutionPause pause(implementation);
    thread->host_thread.join();
  }
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
