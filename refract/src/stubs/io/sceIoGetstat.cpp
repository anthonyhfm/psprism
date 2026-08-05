void sceIoGetstat(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto* path = guest_string(state, state.gpr[4]);
  auto* output = psprecomp::mapped_address(state, state.gpr[5], 88U);
  if (path == nullptr || output == nullptr)
    return;
  const auto resolved = implementation.resolve_path(path);
  std::error_code error;
  const auto status = std::filesystem::status(resolved, error);
  if (error || !std::filesystem::exists(status)) {
    state.gpr[2] = io_error;
    return;
  }
  std::memset(output, 0, 88U);
  const auto mode = std::filesystem::is_directory(status) ? 0x11ffU : 0x21ffU;
  std::memcpy(output, &mode, sizeof(mode));
  const auto size = std::filesystem::is_regular_file(status)
                        ? std::filesystem::file_size(resolved, error)
                        : 0U;
  std::memcpy(output + 8U, &size, sizeof(size));
  std::error_code relative_error;
  const auto relative = std::filesystem::relative(
      resolved, implementation.configuration.disc_root, relative_error);
  if (!relative_error) {
    const auto sector = implementation.disc_sectors.find(
        relative.generic_string());
    if (sector != implementation.disc_sectors.end())
      std::memcpy(output + 64U, &sector->second, sizeof(sector->second));
  }
  state.gpr[2] = error ? io_error : 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
