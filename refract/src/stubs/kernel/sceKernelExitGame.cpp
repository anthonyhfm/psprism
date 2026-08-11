void sceKernelExitGame(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  request_guest_exit(implementation);
  host::request_frontend_exit();
  state.gpr[2] = 0;
  state.stop_reason = psprecomp::StopReason::returned;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
