void sceUtilityMsgDialogUpdate(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  if (implementation.message_dialog_status == 2U) {
    const auto dialog_result = host::poll_dialog_result(
        implementation.message_dialog_id);
    if (!dialog_result) {
      state.gpr[2] = 0U;
      return;
    }
    if (auto* parameters = psprecomp::mapped_address(
            state, implementation.message_dialog_parameters, 572U)) {
      std::uint32_t size{};
      std::memcpy(&size, parameters, sizeof(size));
      const std::uint32_t success{};
      const auto common_result = dialog_result->cancelled
                                     ? utility_cancelled
                                     : success;
      std::memcpy(parameters + 28U, &common_result, sizeof(common_result));
      std::memcpy(parameters + 48U, &success, sizeof(success));
      if (size >= 580U) {
        const std::uint32_t button = dialog_result->cancelled
                                         ? 3U
                                     : dialog_result->affirmative ? 1U : 2U;
        if (auto* complete = psprecomp::mapped_address(
                state, implementation.message_dialog_parameters, size))
          std::memcpy(complete + 576U, &button, sizeof(button));
      }
    }
    implementation.message_dialog_status = 3U;
  }
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
