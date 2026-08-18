void sceKernelUSec2SysClock(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  if (auto* output = guest_pointer<std::uint64_t>(state, state.gpr[5])) {
    *output = state.gpr[4];
  }
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
