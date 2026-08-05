void sceKernelVolatileMemUnlock(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
