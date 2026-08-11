void sceIoRead(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto psp_descriptor = static_cast<int>(state.gpr[4]);
  const auto descriptor = implementation.descriptor(psp_descriptor);
  auto size = static_cast<std::size_t>(state.gpr[6]);
  if (descriptor < 0) {
    state.gpr[2] = io_error;
    return;
  }
  const auto sector_file = implementation.sector_files.contains(psp_descriptor);
  if (sector_file) {
    const auto byte_count = io_state::sector_byte_count(state.gpr[6]);
    if (!byte_count) {
      state.gpr[2] = io_error;
      return;
    }
    size = *byte_count;
  }
  const auto view = implementation.file_views.find(psp_descriptor);
  if (view != implementation.file_views.end()) {
    const auto position = ::lseek(descriptor, 0, SEEK_CUR);
    if (position < 0) {
      state.gpr[2] = io_error;
      return;
    }
    size = io_state::readable_size(
        view->second, static_cast<std::uint64_t>(position), size);
  }
  if (size == 0U) {
    state.gpr[2] = 0U;
    return;
  }
  if (!psprecomp::address_ok(state, state.gpr[5], size)) {
    state.gpr[2] = io_error;
    return;
  }
  auto* buffer = psprecomp::mapped_address(state, state.gpr[5], size);
  const auto result = ::read(descriptor, buffer, size);
  state.gpr[2] =
      result < 0
          ? io_error
          : static_cast<std::uint32_t>(
                sector_file
                    ? io_state::complete_sector_count(
                          static_cast<std::uint64_t>(result))
                    : static_cast<std::uint64_t>(result));
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
