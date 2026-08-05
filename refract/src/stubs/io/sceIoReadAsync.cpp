void sceIoReadAsync(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto psp_descriptor = static_cast<int>(state.gpr[4]);
  const auto descriptor = implementation.descriptor(psp_descriptor);
  const auto size = static_cast<std::size_t>(state.gpr[6]);
  if (descriptor < 0 || !psprecomp::address_ok(state, state.gpr[5], size)) {
    state.gpr[2] = io_error;
    return;
  }
  auto* buffer = psprecomp::mapped_address(state, state.gpr[5], size);
  const auto result = ::read(descriptor, buffer, size);
  implementation.async_results[psp_descriptor] =
      result < 0
          ? static_cast<std::int64_t>(static_cast<std::int32_t>(io_error))
          : result;
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
