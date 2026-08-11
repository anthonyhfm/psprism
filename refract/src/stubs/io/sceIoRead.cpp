void sceIoRead(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto descriptor =
      implementation.descriptor(static_cast<int>(state.gpr[4]));
  auto size = static_cast<std::size_t>(state.gpr[6]);
  if (descriptor < 0) {
    state.gpr[2] = io_error;
    return;
  }
  const auto view =
      implementation.file_views.find(static_cast<int>(state.gpr[4]));
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
  state.gpr[2] = result < 0 ? io_error : static_cast<std::uint32_t>(result);
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
