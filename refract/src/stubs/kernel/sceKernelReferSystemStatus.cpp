void sceKernelReferSystemStatus(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  constexpr std::uint32_t system_status_size = 28U;
  auto* output = psprecomp::mapped_address(state, state.gpr[4],
                                           system_status_size);
  if (output == nullptr)
    return;
  std::memset(output, 0, system_status_size);
  std::memcpy(output, &system_status_size, sizeof(system_status_size));
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
