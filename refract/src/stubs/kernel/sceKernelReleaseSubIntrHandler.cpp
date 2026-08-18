void sceKernelReleaseSubIntrHandler(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto key = (static_cast<std::uint64_t>(state.gpr[4]) << 32U) |
                   state.gpr[5];
  std::shared_ptr<Implementation::SubInterrupt> interrupt;
  {
    std::lock_guard lock(implementation.objects_mutex);
    const auto found = implementation.sub_interrupts.find(key);
    if (found == implementation.sub_interrupts.end()) {
      state.gpr[2] = static_cast<std::uint32_t>(-1);
      return;
    }
    interrupt = found->second;
  }
  {
    std::lock_guard lock(interrupt->mutex);
    interrupt->enabled = false;
    interrupt->cancelled = true;
  }
  interrupt->changed.notify_all();
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
