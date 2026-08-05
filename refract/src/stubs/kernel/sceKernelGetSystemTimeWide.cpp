void sceKernelGetSystemTimeWide(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto value = implementation.elapsed_microseconds();
  state.gpr[2] = static_cast<std::uint32_t>(value);
  state.gpr[3] = static_cast<std::uint32_t>(value >> 32U);
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
