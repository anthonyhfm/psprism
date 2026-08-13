void sceGeBreak(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto mode = state.gpr[4];
  if (mode > 1U) {
    state.gpr[2] = 0x80000107U;
    return;
  }
  std::lock_guard graphics_lock(implementation.graphics.mutex);
  const auto list_id = implementation.ge_scheduler.break_lists(mode);
  state.gpr[2] = mode == 0U && list_id >= 0
                     ? static_cast<std::uint32_t>(list_id)
                     : 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
