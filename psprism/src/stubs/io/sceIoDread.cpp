void sceIoDread(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::lock_guard lock(implementation.objects_mutex);
  const auto found =
      implementation.directories.find(static_cast<int>(state.gpr[4]));
  if (found == implementation.directories.end())
    return;
  if (found->second.next >= found->second.entries.size()) {
    state.gpr[2] = 0;
    return;
  }
  constexpr std::size_t dirent_size = 352;
  auto* output = psprecomp::mapped_address(state, state.gpr[5], dirent_size);
  if (output == nullptr)
    return;
  std::memset(output, 0, dirent_size);
  const auto& entry = found->second.entries[found->second.next++];
  const auto filename = entry.path().filename().string();
  const auto mode = entry.is_directory() ? 0x11ffU : 0x21ffU;
  std::memcpy(output, &mode, sizeof(mode));
  const auto size = entry.is_regular_file() ? entry.file_size() : 0U;
  std::memcpy(output + 8U, &size, sizeof(size));
  std::error_code relative_error;
  const auto relative = std::filesystem::relative(
      entry.path(), implementation.configuration.disc_root,
      relative_error);
  if (!relative_error) {
    const auto sector = implementation.disc_sectors.find(
        relative.generic_string());
    if (sector != implementation.disc_sectors.end())
      std::memcpy(output + 64U, &sector->second, sizeof(sector->second));
  }
  std::memcpy(output + 88U, filename.c_str(),
              std::min<std::size_t>(filename.size(), 255U));
  state.gpr[2] = 1;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
