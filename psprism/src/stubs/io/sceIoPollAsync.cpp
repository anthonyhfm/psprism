void sceIoPollAsync(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto found =
      implementation.async_results.find(static_cast<int>(state.gpr[4]));
  if (found == implementation.async_results.end()) {
    state.gpr[2] = 1U;
    return;
  }
  if (auto* output = psprecomp::mapped_address(state, state.gpr[5],
                                               sizeof(std::int64_t)))
    std::memcpy(output, &found->second, sizeof(found->second));
  implementation.async_results.erase(found);
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
