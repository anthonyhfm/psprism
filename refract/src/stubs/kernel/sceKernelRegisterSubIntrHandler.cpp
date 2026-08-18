void sceKernelRegisterSubIntrHandler(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto interrupt_number = state.gpr[4];
  const auto sub_interrupt_number = state.gpr[5];
  const auto handler = state.gpr[6];
  const auto argument = state.gpr[7];
  if (handler == 0U) {
    state.gpr[2] = static_cast<std::uint32_t>(-1);
    return;
  }
  const auto key = (static_cast<std::uint64_t>(interrupt_number) << 32U) |
                   sub_interrupt_number;
  auto interrupt = std::make_shared<Implementation::SubInterrupt>();
  interrupt->interrupt_number = interrupt_number;
  interrupt->sub_interrupt_number = sub_interrupt_number;
  interrupt->handler = handler;
  interrupt->argument = argument;
  {
    std::lock_guard lock(implementation.objects_mutex);
    if (!implementation.sub_interrupts.emplace(key, interrupt).second) {
      state.gpr[2] = static_cast<std::uint32_t>(-1);
      return;
    }
  }
  state.gpr[2] = 0U;
  if (implementation.verbose)
    std::fprintf(stderr,
                 "[psprism:intr] register int=%u sub=%u handler=%08x "
                 "argument=%08x\n",
                 interrupt_number, sub_interrupt_number, handler, argument);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
