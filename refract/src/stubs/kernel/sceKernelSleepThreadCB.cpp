void sceKernelSleepThreadCB(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  host::sleep_microseconds(1000U);
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
