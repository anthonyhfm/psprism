void sceKernelCpuResumeIntrWithSync(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  guest_interrupts_enabled = state.gpr[4] != 0U;
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
