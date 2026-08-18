void sceIoChdir(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto* path = guest_string(state, state.gpr[4]);
  if (path == nullptr)
    return;
  const auto resolved = implementation.resolve_path(path);
  if (!std::filesystem::is_directory(resolved)) {
    state.gpr[2] = io_error;
  } else {
    std::lock_guard lock(implementation.io_mutex);
    implementation.current_directory = resolved;
    state.gpr[2] = 0;
  }
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
