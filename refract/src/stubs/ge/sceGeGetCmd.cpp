void sceGeGetCmd(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  if (state.gpr[4] >= 256U) {
    state.gpr[2] = 0x80000102U;
    return;
  }
  std::lock_guard graphics_lock(implementation.graphics.mutex);
  state.gpr[2] = implementation.graphics.read_command(state.gpr[4]);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
