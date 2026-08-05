void sceKernelDeleteEventFlag(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::lock_guard lock(implementation.objects_mutex);
  state.gpr[2] =
      implementation.event_flags.erase(static_cast<int>(state.gpr[4]))
          ? 0U
          : unimplemented;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
