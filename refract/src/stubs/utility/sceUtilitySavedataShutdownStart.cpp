void sceUtilitySavedataShutdownStart(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  host::dismiss_dialog(implementation.savedata_dialog_id);
  implementation.savedata_status = 4U;
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
