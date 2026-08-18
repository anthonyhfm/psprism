void sceIoReadAsync(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto psp_descriptor = static_cast<int>(state.gpr[4]);
  int descriptor = -1;
  bool sector_file = false;
  std::optional<io_state::FileView> file_view;
  {
    std::lock_guard lock(implementation.io_mutex);
    if (psp_descriptor >= 0 && psp_descriptor <= 2) {
      descriptor = psp_descriptor;
    } else {
      const auto found = implementation.files.find(psp_descriptor);
      if (found != implementation.files.end())
        descriptor = found->second;
    }
    sector_file = implementation.sector_files.contains(psp_descriptor);
    const auto view = implementation.file_views.find(psp_descriptor);
    if (view != implementation.file_views.end())
      file_view = view->second;
  }
  auto size = static_cast<std::size_t>(state.gpr[6]);
  if (descriptor < 0) {
    state.gpr[2] = io_error;
    return;
  }
  if (sector_file) {
    const auto byte_count = io_state::sector_byte_count(state.gpr[6]);
    if (!byte_count) {
      state.gpr[2] = io_error;
      return;
    }
    size = *byte_count;
  }
  if (file_view) {
    const auto position = ::lseek(descriptor, 0, SEEK_CUR);
    if (position < 0) {
      state.gpr[2] = io_error;
      return;
    }
    size = io_state::readable_size(
        *file_view, static_cast<std::uint64_t>(position), size);
  }
  if (size == 0U) {
    std::lock_guard lock(implementation.io_mutex);
    implementation.async_results[psp_descriptor] = 0;
    state.gpr[2] = 0U;
    return;
  }
  if (!psprecomp::address_ok(state, state.gpr[5], size)) {
    state.gpr[2] = io_error;
    return;
  }
  auto* buffer = psprecomp::mapped_address(state, state.gpr[5], size);
  const auto result = ::read(descriptor, buffer, size);
  {
    std::lock_guard lock(implementation.io_mutex);
    implementation.async_results[psp_descriptor] =
        result < 0
            ? static_cast<std::int64_t>(static_cast<std::int32_t>(io_error))
            : static_cast<std::int64_t>(
                  sector_file
                      ? io_state::complete_sector_count(
                            static_cast<std::uint64_t>(result))
                      : static_cast<std::uint64_t>(result));
  }
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
