void sceDisplayWaitVblank(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
