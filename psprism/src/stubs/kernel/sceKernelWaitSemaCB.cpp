void sceKernelWaitSemaCB(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  sceKernelWaitSema(implementation, state);
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
