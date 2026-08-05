void sceUtilityOskShutdownStart(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  host::dismiss_dialog(implementation.osk_dialog_id);
  if (auto* parameters = psprecomp::mapped_address(
          state, implementation.osk_parameters, 60U)) {
    const std::uint32_t local_state = 5U;
    std::memcpy(parameters + 56U, &local_state, sizeof(local_state));
  }
  implementation.osk_status = 4U;
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
