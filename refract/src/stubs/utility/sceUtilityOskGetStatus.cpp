void sceUtilityOskGetStatus(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto status = implementation.osk_status.load();
  state.gpr[2] = status;
  if (status == 1U) {
    implementation.osk_status = 2U;
    if (auto* parameters = psprecomp::mapped_address(
            state, implementation.osk_parameters, 60U)) {
      const std::uint32_t local_state = 3U;
      std::memcpy(parameters + 56U, &local_state, sizeof(local_state));
    }
  } else if (status == 4U) {
    implementation.osk_status = 0U;
    implementation.active_utility = 0U;
  }
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
