void sceUtilityMsgDialogGetStatus(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto status = implementation.message_dialog_status.load();
  state.gpr[2] = status;
  if (status == 1U)
    implementation.message_dialog_status = 2U;
  else if (status == 4U) {
    implementation.message_dialog_status = 0U;
    implementation.active_utility = 0U;
  }
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
