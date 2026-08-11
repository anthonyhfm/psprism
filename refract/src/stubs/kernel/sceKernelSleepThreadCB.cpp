void sceKernelSleepThreadCB(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  {
    GuestExecutionPause pause(implementation);
    std::unique_lock lock(implementation.exit_mutex);
    implementation.exit_changed.wait_for(
        lock, std::chrono::microseconds(1000U),
        [&] { return implementation.exit_requested.load(); });
  }
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
