void sceIoClose(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto found =
      implementation.files.find(static_cast<int>(state.gpr[4]));
  if (found == implementation.files.end())
    return;
  state.gpr[2] = ::close(found->second) == 0 ? 0U : io_error;
  implementation.files.erase(found);
  implementation.file_views.erase(static_cast<int>(state.gpr[4]));
  implementation.sector_files.erase(static_cast<int>(state.gpr[4]));
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
