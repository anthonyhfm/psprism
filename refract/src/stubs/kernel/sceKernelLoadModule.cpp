void sceKernelLoadModule(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto* path = guest_string(state, state.gpr[4]);
  if (path == nullptr) {
    state.gpr[2] = io_error;
    return;
  }
  const auto resolved = implementation.resolve_path(path);
  if (implementation.verbose) {
    std::fprintf(stderr,
                 "[psprism:module] load path=%s flags=%08x option=%08x\n",
                 path, state.gpr[5], state.gpr[6]);
  }
  std::error_code error;
  if (!std::filesystem::is_regular_file(resolved, error)) {
    state.gpr[2] = io_error;
    return;
  }
  std::lock_guard lock(implementation.objects_mutex);
  const auto uid = implementation.allocate_uid();
  implementation.modules.emplace(uid,
                                 Implementation::Module{resolved, false});
  state.gpr[2] = static_cast<std::uint32_t>(uid);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
