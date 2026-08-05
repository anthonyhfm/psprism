void sceUtilityOskInitStart(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  auto* parameters = psprecomp::mapped_address(state, state.gpr[4], 64U);
  if (parameters == nullptr) {
    state.gpr[2] = 0x80110002U;
    return;
  }
  std::uint32_t expected{};
  if (!implementation.active_utility.compare_exchange_strong(expected, 3U)) {
    state.gpr[2] = utility_busy;
    return;
  }
  std::uint32_t language{};
  std::uint32_t button_swap{};
  std::uint32_t count{};
  std::uint32_t data_address{};
  std::memcpy(&language, parameters + 4U, sizeof(language));
  std::memcpy(&button_swap, parameters + 8U, sizeof(button_swap));
  std::memcpy(&count, parameters + 48U, sizeof(count));
  std::memcpy(&data_address, parameters + 52U, sizeof(data_address));
  if (count == 0U || count > 16U || psprecomp::mapped_address(
          state, data_address, static_cast<std::size_t>(count) * 52U) == nullptr) {
    implementation.active_utility = 0U;
    state.gpr[2] = 0x80110002U;
    return;
  }
  implementation.osk_parameters = state.gpr[4];
  implementation.osk_dialog_id = implementation.next_dialog_id++;
  implementation.osk_status = 1U;
  const std::uint32_t local_state = 1U;
  std::memcpy(parameters + 56U, &local_state, sizeof(local_state));
  const auto text = utility_text(language);
  host::DialogModel dialog;
  dialog.id = implementation.osk_dialog_id;
  dialog.kind = host::DialogKind::osk;
  dialog.title = text.keyboard;
  dialog.confirm_with_cross = button_swap != 0U;
  dialog.accept_label = text.accept;
  dialog.cancel_label = text.back;
  for (std::uint32_t index = 0; index < count; ++index) {
    const auto entry_address = data_address + index * 52U;
    auto* entry = psprecomp::mapped_address(state, entry_address, 52U);
    std::uint32_t input_type{};
    std::uint32_t description{};
    std::uint32_t input{};
    std::uint32_t output_length{};
    std::uint32_t output_limit{};
    std::memcpy(&input_type, entry + 16U, sizeof(input_type));
    std::memcpy(&description, entry + 28U, sizeof(description));
    std::memcpy(&input, entry + 32U, sizeof(input));
    std::memcpy(&output_length, entry + 36U, sizeof(output_length));
    std::memcpy(&output_limit, entry + 48U, sizeof(output_limit));
    host::OskField field;
    field.label = utf16_to_utf8(guest_utf16(state, description));
    field.text = guest_utf16(state, input,
                             std::min<std::uint32_t>(output_length, 4096U));
    field.limit = std::min(output_limit,
                           output_length == 0U ? output_limit
                                              : output_length - 1U);
    field.input_type = input_type;
    dialog.fields.push_back(std::move(field));
  }
  host::present_dialog(std::move(dialog));
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
