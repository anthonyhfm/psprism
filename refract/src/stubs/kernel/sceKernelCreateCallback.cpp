void sceKernelCreateCallback(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::lock_guard lock(implementation.objects_mutex);
  const auto uid = implementation.allocate_uid();
  implementation.callbacks.emplace(
      uid, Implementation::Callback{state.gpr[5], state.gpr[6]});
  state.gpr[2] = static_cast<std::uint32_t>(uid);
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
