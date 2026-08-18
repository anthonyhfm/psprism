void sceKernelSetAlarm(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto delay = state.gpr[4];
  auto alarm = std::make_shared<Implementation::Alarm>();
  alarm->handler = state.gpr[5];
  alarm->common = state.gpr[6];
  int uid{};
  {
    std::lock_guard lock(implementation.objects_mutex);
    uid = implementation.allocate_uid();
    implementation.alarms.emplace(uid, alarm);
  }
  const auto callback_state = state;
  alarm->host_thread = std::jthread(
      [alarm, delay, callback_state, &implementation] {
        auto next_delay = delay;
        for (;;) {
          {
            std::unique_lock lock(alarm->mutex);
            if (alarm->changed.wait_for(
                    lock, std::chrono::microseconds(next_delay), [&] {
                      return alarm->cancelled ||
                             implementation.exit_requested.load(
                                 std::memory_order_relaxed);
                    }))
              return;
          }
          std::uint32_t reschedule{};
          {
            GuestExecutionLock execution_lock(implementation);
            if (!execution_lock.locked())
              return;
            auto active_state = callback_state;
            if (!dispatch_guest_callback(
                    implementation, active_state, alarm->handler,
                    alarm->common, 0U, 0U, &reschedule))
              return;
          }
          ++alarm->firings;
          if (implementation.verbose && alarm->firings <= 8U)
            std::fprintf(stderr,
                         "[psprism:alarm] fire handler=%08x count=%u "
                         "reschedule=%u\n",
                         alarm->handler, alarm->firings, reschedule);
          if (reschedule == 0U)
            return;
          next_delay = reschedule;
        }
      });
  state.gpr[2] = static_cast<std::uint32_t>(uid);
  if (implementation.verbose)
    std::fprintf(stderr,
                 "[psprism:alarm] set uid=%d delay=%u handler=%08x "
                 "common=%08x\n",
                 uid, delay, alarm->handler, alarm->common);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
