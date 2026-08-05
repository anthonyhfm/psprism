void sceKernelDeleteMutex(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::lock_guard lock(implementation.objects_mutex);
  if (implementation.mutexes.erase(static_cast<int>(state.gpr[4])) > 0) {
    state.gpr[2] = 0;
    return;
  }
  state.gpr[2] = unimplemented;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
