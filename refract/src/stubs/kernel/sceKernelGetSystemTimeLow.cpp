void sceKernelGetSystemTimeLow(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  state.gpr[2] =
      static_cast<std::uint32_t>(implementation.elapsed_microseconds());
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
