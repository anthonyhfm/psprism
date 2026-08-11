void sceGeUnsetCallback(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::lock_guard lock(implementation.objects_mutex);
  implementation.ge_callbacks.erase(static_cast<int>(state.gpr[4]));
  state.gpr[2] = 0;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
