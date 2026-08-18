void sceKernelLibcTime(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  const auto seconds = static_cast<std::uint32_t>(host::unix_seconds());
  if (auto* destination = guest_pointer<std::uint32_t>(state, state.gpr[4])) {
    *destination = seconds;
  }
  state.gpr[2] = seconds;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
