void sceKernelExitDeleteThread(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  state.gpr[2] = 0;
  state.stop_reason = psprecomp::StopReason::returned;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
