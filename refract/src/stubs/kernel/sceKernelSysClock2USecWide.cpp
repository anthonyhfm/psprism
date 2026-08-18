void sceKernelSysClock2USecWide(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  const auto ticks = static_cast<std::uint64_t>(state.gpr[4]) |
                     (static_cast<std::uint64_t>(state.gpr[5]) << 32U);
  if (auto* low = guest_pointer<std::uint32_t>(state, state.gpr[6]))
    *low = static_cast<std::uint32_t>(ticks & 0xffffffffU);
  if (auto* high = guest_pointer<std::uint32_t>(state, state.gpr[7]))
    *high = static_cast<std::uint32_t>(ticks >> 32U);
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
