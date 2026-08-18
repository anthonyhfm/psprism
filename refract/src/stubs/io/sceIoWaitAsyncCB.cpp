void sceIoWaitAsyncCB(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::int64_t result{};
  {
    std::lock_guard lock(implementation.io_mutex);
    const auto found =
        implementation.async_results.find(static_cast<int>(state.gpr[4]));
    if (found == implementation.async_results.end()) {
      state.gpr[2] = io_error;
      return;
    }
    result = found->second;
    implementation.async_results.erase(found);
  }
  if (auto* output = psprecomp::mapped_address(state, state.gpr[5],
                                               sizeof(std::int64_t)))
    std::memcpy(output, &result, sizeof(result));
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
