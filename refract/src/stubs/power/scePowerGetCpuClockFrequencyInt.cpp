void scePowerGetCpuClockFrequencyInt(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  state.gpr[2] = 333U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
