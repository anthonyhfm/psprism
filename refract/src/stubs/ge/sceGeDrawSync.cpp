void sceGeDrawSync(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  if (state.gpr[4] > 1U) {
    state.gpr[2] = 0x80000107U;
    return;
  }
  std::lock_guard graphics_lock(implementation.graphics.mutex);
  state.gpr[2] = static_cast<std::uint32_t>(
      implementation.ge_scheduler.draw_status());
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
