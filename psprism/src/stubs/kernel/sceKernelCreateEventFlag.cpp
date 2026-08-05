void sceKernelCreateEventFlag(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  auto event = std::make_shared<Implementation::EventFlag>();
  event->bits = state.gpr[6];
  std::lock_guard lock(implementation.objects_mutex);
  const auto uid = implementation.allocate_uid();
  implementation.event_flags.emplace(uid, event);
  state.gpr[2] = static_cast<std::uint32_t>(uid);
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
