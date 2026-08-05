void sceDisplaySetMode(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  static_cast<void>(state);
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
