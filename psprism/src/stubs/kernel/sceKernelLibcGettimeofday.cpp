void sceKernelLibcGettimeofday(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  if (auto* destination =
          psprecomp::mapped_address(state, state.gpr[4], 8U)) {
    const auto elapsed = implementation.elapsed_microseconds();
    const auto seconds = static_cast<std::uint32_t>(
        implementation.start_unix_seconds + elapsed / 1000000U);
    const auto microseconds = static_cast<std::uint32_t>(elapsed % 1000000U);
    std::memcpy(destination, &seconds, sizeof(seconds));
    std::memcpy(destination + 4U, &microseconds, sizeof(microseconds));
  } else if (state.gpr[4] != 0) {
    state.gpr[2] = 0U;
    return;
  }
  if (auto* timezone = psprecomp::mapped_address(state, state.gpr[5], 8U))
    std::memset(timezone, 0, 8U);
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
