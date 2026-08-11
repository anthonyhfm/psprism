void sceKernelDelayThreadCB(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  static thread_local std::uint32_t logged_delays{};
  if (implementation.verbose && logged_delays++ < 8U) {
    std::fprintf(stderr, "[psprism:thread] delay-cb thread=%d usec=%u\n",
                 current_thread_id, state.gpr[4]);
  }
  {
    GuestExecutionPause pause(implementation);
    std::unique_lock lock(implementation.exit_mutex);
    implementation.exit_changed.wait_for(
        lock, std::chrono::microseconds(state.gpr[4]),
        [&] { return implementation.exit_requested.load(); });
  }
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
