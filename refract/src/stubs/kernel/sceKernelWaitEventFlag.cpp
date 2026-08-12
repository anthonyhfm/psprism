void sceKernelWaitEventFlag(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::shared_ptr<Implementation::EventFlag> event;
  {
    std::lock_guard lock(implementation.objects_mutex);
    const auto found =
        implementation.event_flags.find(static_cast<int>(state.gpr[4]));
    if (found == implementation.event_flags.end())
      return;
    event = found->second;
  }
  const auto pattern = state.gpr[5];
  const auto mode = state.gpr[6];
  const auto matched = [&] {
    return (mode & 1U) != 0 ? (event->bits & pattern) != 0
                            : (event->bits & pattern) == pattern;
  };
  std::unique_lock lock(event->mutex);
  std::uint32_t observed{};
  {
    GuestExecutionPause pause(implementation);
    ++event->waiting_threads;
    event->changed.wait(
        lock, [&] { return matched() || implementation.exit_requested; });
    --event->waiting_threads;
    observed = event->bits;
    if ((mode & 0x20U) != 0)
      event->bits = 0;
    else if ((mode & 0x10U) != 0)
      event->bits &= ~pattern;
    lock.unlock();
  }
  if (auto* output = guest_pointer<std::uint32_t>(state, state.gpr[7]))
    *output = observed;
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
