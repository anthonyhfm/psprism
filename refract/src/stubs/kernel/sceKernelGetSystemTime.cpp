void sceKernelGetSystemTime(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto ticks = implementation.start_unix_seconds * 1000000ULL +
                     implementation.elapsed_microseconds();
  if (auto* output = guest_pointer<std::uint64_t>(state, state.gpr[4])) {
    *output = ticks;
    state.gpr[2] = 0U;
  } else {
    state.gpr[2] = ticks & 0xffffffffU;
  }
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
