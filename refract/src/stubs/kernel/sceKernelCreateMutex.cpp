void sceKernelCreateMutex(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  auto mutex = std::make_shared<Implementation::Mutex>();
  if (const auto* name = guest_string(state, state.gpr[4]))
    mutex->name = name;
  std::lock_guard lock(implementation.objects_mutex);
  const auto uid = implementation.allocate_uid();
  implementation.mutexes.emplace(uid, mutex);
  state.gpr[2] = static_cast<std::uint32_t>(uid);
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
