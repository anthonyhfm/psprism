void sceKernelEnableSubIntr(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto interrupt_number = state.gpr[4];
  const auto sub_interrupt_number = state.gpr[5];
  const auto key = (static_cast<std::uint64_t>(interrupt_number) << 32U) |
                   sub_interrupt_number;
  std::shared_ptr<Implementation::SubInterrupt> interrupt;
  {
    std::lock_guard lock(implementation.objects_mutex);
    const auto found = implementation.sub_interrupts.find(key);
    if (found == implementation.sub_interrupts.end()) {
      state.gpr[2] = static_cast<std::uint32_t>(-1);
      return;
    }
    interrupt = found->second;
  }
  {
    std::lock_guard lock(interrupt->mutex);
    interrupt->enabled = true;
  }
  interrupt->changed.notify_all();

  // Interrupt 30 is the PSP display interrupt.  Deliver sub-interrupts at
  // the display's 60 Hz cadence so games using a vblank ISR observe the same
  // frame clock as games using sceDisplayWaitVblank directly.
  if (interrupt_number == 30U && !interrupt->host_thread.joinable()) {
    const auto callback_state = state;
    interrupt->host_thread = std::jthread(
        [interrupt, callback_state, &implementation] {
          constexpr auto vblank_period = std::chrono::microseconds(16667U);
          for (;;) {
            {
              std::unique_lock lock(interrupt->mutex);
              interrupt->changed.wait(lock, [&] {
                return interrupt->enabled || interrupt->cancelled ||
                       implementation.exit_requested.load(
                           std::memory_order_relaxed);
              });
              if (interrupt->cancelled ||
                  implementation.exit_requested.load(
                      std::memory_order_relaxed))
                return;
              if (interrupt->changed.wait_for(lock, vblank_period, [&] {
                    return !interrupt->enabled || interrupt->cancelled ||
                           implementation.exit_requested.load(
                               std::memory_order_relaxed);
                  }))
                continue;
            }
            {
              GuestExecutionLock execution_lock(implementation);
              if (!execution_lock.locked())
                return;
              auto active_state = callback_state;
              if (!dispatch_guest_callback(
                      implementation, active_state, interrupt->handler,
                      interrupt->sub_interrupt_number, interrupt->argument))
                return;
            }
            ++interrupt->firings;
            if (implementation.verbose && interrupt->firings <= 8U)
              std::fprintf(stderr,
                           "[psprism:intr] fire int=%u sub=%u handler=%08x "
                           "count=%u\n",
                           interrupt->interrupt_number,
                           interrupt->sub_interrupt_number,
                           interrupt->handler, interrupt->firings);
          }
        });
  }
  state.gpr[2] = 0U;
  if (implementation.verbose)
    std::fprintf(stderr, "[psprism:intr] enable int=%u sub=%u\n",
                 interrupt_number, sub_interrupt_number);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
