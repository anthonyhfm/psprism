void scePowerSetCpuClockFrequency(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  state.gpr[2] = 0;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
