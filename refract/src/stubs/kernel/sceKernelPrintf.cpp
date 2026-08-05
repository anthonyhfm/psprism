void sceKernelPrintf(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  if (implementation.verbose)
    if (const auto* format = guest_string(state, state.gpr[4]))
      std::fprintf(stderr, "[guest] %s", format);
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
