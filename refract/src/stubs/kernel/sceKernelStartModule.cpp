void sceKernelStartModule(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  if (implementation.verbose) {
    std::fprintf(stderr,
                 "[psprism:module] start uid=%u args=%u argp=%08x "
                 "status=%08x option=%08x\n",
                 state.gpr[4], state.gpr[5], state.gpr[6], state.gpr[7],
                 psprecomp::load32(state, state.gpr[29] + 16U));
  }
  {
    std::lock_guard lock(implementation.objects_mutex);
    const auto found =
        implementation.modules.find(static_cast<int>(state.gpr[4]));
    if (found == implementation.modules.end()) {
      state.gpr[2] = unimplemented;
      return;
    }
    found->second.started = true;
  }
  if (auto* status = guest_pointer<std::uint32_t>(state, state.gpr[7]))
    *status = 0U;
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
