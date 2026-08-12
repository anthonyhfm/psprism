void sceKernelPollEventFlag(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::shared_ptr<Implementation::EventFlag> event;
  {
    std::lock_guard lock(implementation.objects_mutex);
    const auto found =
        implementation.event_flags.find(static_cast<int>(state.gpr[4]));
    if (found == implementation.event_flags.end()) {
      state.gpr[2] = unimplemented;
      return;
    }
    event = found->second;
  }
  const auto pattern = state.gpr[5];
  const auto mode = state.gpr[6];
  std::lock_guard lock(event->mutex);
  const auto observed = event->bits;
  if (auto* output = guest_pointer<std::uint32_t>(state, state.gpr[7]))
    *output = observed;
  const auto matched = (mode & 1U) != 0U
                           ? (observed & pattern) != 0U
                           : (observed & pattern) == pattern;
  if (!matched) {
    state.gpr[2] = event_flag_condition;
    return;
  }
  if ((mode & 0x20U) != 0U)
    event->bits = 0U;
  else if ((mode & 0x10U) != 0U)
    event->bits &= ~pattern;
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
