void sceKernelCancelAlarm(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::shared_ptr<Implementation::Alarm> alarm;
  {
    std::lock_guard lock(implementation.objects_mutex);
    const auto found =
        implementation.alarms.find(static_cast<int>(state.gpr[4]));
    if (found == implementation.alarms.end()) {
      state.gpr[2] = static_cast<std::uint32_t>(-1);
      return;
    }
    alarm = found->second;
  }
  {
    std::lock_guard lock(alarm->mutex);
    alarm->cancelled = true;
  }
  alarm->changed.notify_all();
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
