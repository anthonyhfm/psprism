void sceUtilityMsgDialogInitStart(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  auto* parameters = psprecomp::mapped_address(state, state.gpr[4], 572U);
  if (parameters == nullptr) {
    state.gpr[2] = 0x80110002U;
    return;
  }
  std::uint32_t expected{};
  if (!implementation.active_utility.compare_exchange_strong(expected, 2U)) {
    state.gpr[2] = utility_busy;
    return;
  }
  implementation.message_dialog_parameters = state.gpr[4];
  implementation.message_dialog_id = implementation.next_dialog_id++;
  implementation.message_dialog_status = 1U;
  std::uint32_t size{};
  std::uint32_t language{};
  std::uint32_t button_swap{};
  std::uint32_t mode{};
  std::uint32_t error_value{};
  std::memcpy(&size, parameters, sizeof(size));
  std::memcpy(&language, parameters + 4U, sizeof(language));
  std::memcpy(&button_swap, parameters + 8U, sizeof(button_swap));
  std::memcpy(&mode, parameters + 52U, sizeof(mode));
  std::memcpy(&error_value, parameters + 56U, sizeof(error_value));
  std::uint32_t options{};
  if (size >= 576U) std::memcpy(&options, parameters + 572U, sizeof(options));
  const auto text = utility_text(language);
  host::DialogModel dialog;
  dialog.id = implementation.message_dialog_id;
  dialog.kind = host::DialogKind::message;
  dialog.title = text.message;
  if (mode == 0U) {
    char formatted[64]{};
    std::snprintf(formatted, sizeof(formatted), "Error 0x%08X", error_value);
    dialog.message = formatted;
  } else {
    dialog.message = fixed_string(parameters + 60U, 512U);
  }
  dialog.confirm_with_cross = button_swap != 0U;
  dialog.yes_no = (options & 0x10U) != 0U;
  dialog.default_no = (options & 0x100U) != 0U;
  dialog.accept_label = text.accept;
  dialog.cancel_label = text.back;
  dialog.yes_label = text.yes;
  dialog.no_label = text.no;
  host::present_dialog(std::move(dialog));
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
