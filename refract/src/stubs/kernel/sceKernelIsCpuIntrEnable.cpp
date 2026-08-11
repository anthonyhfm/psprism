void sceKernelIsCpuIntrEnable(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  state.gpr[2] = guest_interrupts_enabled ? 1U : 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
