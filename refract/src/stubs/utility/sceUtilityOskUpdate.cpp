void sceUtilityOskUpdate(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  if (implementation.osk_status != 2U) {
    state.gpr[2] = 0U;
    return;
  }
  const auto result = host::poll_dialog_result(implementation.osk_dialog_id);
  if (!result) {
    state.gpr[2] = 0U;
    return;
  }
  auto* parameters = psprecomp::mapped_address(
      state, implementation.osk_parameters, 60U);
  if (parameters != nullptr) {
    std::uint32_t count{};
    std::uint32_t data_address{};
    std::memcpy(&count, parameters + 48U, sizeof(count));
    std::memcpy(&data_address, parameters + 52U, sizeof(data_address));
    for (std::uint32_t index = 0;
         index < count && index < result->field_text.size(); ++index) {
      auto* entry = psprecomp::mapped_address(
          state, data_address + index * 52U, 52U);
      if (entry == nullptr) break;
      std::uint32_t output_length{};
      std::uint32_t output_address{};
      std::uint32_t output_limit{};
      std::uint32_t input_address{};
      std::memcpy(&input_address, entry + 32U, sizeof(input_address));
      std::memcpy(&output_length, entry + 36U, sizeof(output_length));
      std::memcpy(&output_address, entry + 40U, sizeof(output_address));
      std::memcpy(&output_limit, entry + 48U, sizeof(output_limit));
      const auto maximum = std::min(output_length,
          output_limit == 0U ? output_length : output_limit + 1U);
      const auto original = guest_utf16(state, input_address, maximum);
      auto* output = psprecomp::mapped_address(
          state, output_address, static_cast<std::size_t>(maximum) * 2U);
      if (output != nullptr && maximum != 0U) {
        const auto length = std::min<std::size_t>(
            result->field_text[index].size(), maximum - 1U);
        std::memset(output, 0, static_cast<std::size_t>(maximum) * 2U);
        std::memcpy(output, result->field_text[index].data(), length * 2U);
      }
      const std::uint32_t field_result = result->cancelled
                                             ? 1U
                                         : original == result->field_text[index]
                                             ? 0U
                                             : 2U;
      std::memcpy(entry + 44U, &field_result, sizeof(field_result));
    }
    const std::uint32_t local_state = 4U;
    std::memcpy(parameters + 56U, &local_state, sizeof(local_state));
    const auto common_result = result->cancelled ? utility_cancelled : 0U;
    std::memcpy(parameters + 28U, &common_result, sizeof(common_result));
  }
  implementation.osk_status = 3U;
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
