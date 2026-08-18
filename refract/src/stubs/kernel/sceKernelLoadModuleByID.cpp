void sceKernelLoadModuleByID(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto file_uid = static_cast<int>(state.gpr[4]);
  std::lock_guard lock(implementation.objects_mutex);
  if (!implementation.files.contains(file_uid)) {
    state.gpr[2] = io_error;
    return;
  }
  const auto module_uid = implementation.allocate_uid();
  implementation.modules.emplace(
      module_uid,
      Implementation::Module{"fd:" + std::to_string(file_uid), false});
  if (implementation.verbose) {
    std::fprintf(stderr,
                 "[psprism:module] load-by-id fd=%d flags=%08x "
                 "option=%08x uid=%d\n",
                 file_uid, state.gpr[5], state.gpr[6], module_uid);
  }
  state.gpr[2] = static_cast<std::uint32_t>(module_uid);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
