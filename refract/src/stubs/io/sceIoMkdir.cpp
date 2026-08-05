void sceIoMkdir(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto* path = guest_string(state, state.gpr[4]);
  if (path == nullptr)
    return;
  std::error_code error;
  const auto created = std::filesystem::create_directories(
      implementation.resolve_path(path), error);
  state.gpr[2] =
      !error && (created || std::filesystem::is_directory(
                                implementation.resolve_path(path)))
          ? 0U
          : io_error;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
