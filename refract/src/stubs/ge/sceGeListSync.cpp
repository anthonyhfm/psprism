void sceGeListSync(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto list_id = static_cast<int>(state.gpr[4]);
  const auto mode = state.gpr[5];
  std::lock_guard graphics_lock(implementation.graphics.mutex);
  if (mode > 1U) {
    state.gpr[2] = 0x80000107U;
    return;
  }
  if (implementation.ge_scheduler.find(list_id) == nullptr) {
    state.gpr[2] = 0x80000100U;
    return;
  }
  state.gpr[2] = static_cast<std::uint32_t>(
      implementation.ge_scheduler.status(list_id));
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
