void sceKernelUnloadModule(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  if (implementation.verbose) {
    std::fprintf(stderr, "[psprism:module] unload uid=%u\n", state.gpr[4]);
  }
  std::lock_guard lock(implementation.objects_mutex);
  const auto erased =
      implementation.modules.erase(static_cast<int>(state.gpr[4]));
  state.gpr[2] = erased == 0U ? unimplemented : 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
