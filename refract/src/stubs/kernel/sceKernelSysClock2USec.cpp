void sceKernelSysClock2USec(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  const auto* clock = guest_pointer<std::uint64_t>(state, state.gpr[4]);
  if (clock != nullptr) {
    const auto ticks = *clock;
    if (auto* low = guest_pointer<std::uint32_t>(state, state.gpr[5]))
      *low = static_cast<std::uint32_t>(ticks & 0xffffffffU);
    if (auto* high = guest_pointer<std::uint32_t>(state, state.gpr[6]))
      *high = static_cast<std::uint32_t>(ticks >> 32U);
  }
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
