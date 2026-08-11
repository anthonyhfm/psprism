void sceIoCloseAsync(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto psp_descriptor = static_cast<int>(state.gpr[4]);
  const auto found = implementation.files.find(psp_descriptor);
  if (found == implementation.files.end())
    return;
  const auto result = ::close(found->second) == 0 ? 0U : io_error;
  implementation.files.erase(found);
  implementation.file_views.erase(psp_descriptor);
  implementation.async_results[psp_descriptor] =
      static_cast<std::int64_t>(static_cast<std::int32_t>(result));
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
