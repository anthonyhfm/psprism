void sceIoDopen(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto* path = guest_string(state, state.gpr[4]);
  if (path == nullptr)
    return;
  const auto resolved = implementation.resolve_path(path);
  std::error_code error;
  Implementation::Directory directory;
  for (std::filesystem::directory_iterator iterator(resolved, error), end;
       !error && iterator != end; iterator.increment(error)) {
    directory.entries.push_back(*iterator);
  }
  if (error) {
    state.gpr[2] = io_error;
    return;
  }
  std::sort(directory.entries.begin(), directory.entries.end(),
            [](const auto& left, const auto& right) {
              return left.path().filename() < right.path().filename();
            });
  std::lock_guard lock(implementation.io_mutex);
  const auto uid = implementation.next_file++;
  implementation.directories.emplace(uid, std::move(directory));
  state.gpr[2] = static_cast<std::uint32_t>(uid);
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
