void sceDisplayGetFramePerSec(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  state.gpr[2] = 0x41efc71cU; // 59.940060f
  state.fpr[0] = state.gpr[2];
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
