void sceUmdRegisterUMDCallBack(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  dispatch_notify_callback(implementation, state, static_cast<int>(state.gpr[4]),
                          0x32U);
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
