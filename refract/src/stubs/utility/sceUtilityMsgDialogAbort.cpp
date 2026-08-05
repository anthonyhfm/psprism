void sceUtilityMsgDialogAbort(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  if (implementation.message_dialog_status == 0U) {
    state.gpr[2] = 0x80110002U;
    return;
  }
  host::dismiss_dialog(implementation.message_dialog_id);
  if (auto* parameters = psprecomp::mapped_address(
          state, implementation.message_dialog_parameters, 52U)) {
    std::memcpy(parameters + 28U, &utility_cancelled,
                sizeof(utility_cancelled));
  }
  implementation.message_dialog_status = 3U;
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
