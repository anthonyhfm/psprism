void sceKernelVolatileMemTryLock(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  constexpr std::uint32_t volatile_address = 0x08400000U;
  constexpr std::uint32_t volatile_size = 4U * 1024U * 1024U;
  if (auto* output = guest_pointer<std::uint32_t>(state, state.gpr[5]))
    *output = volatile_address;
  if (auto* output = guest_pointer<std::uint32_t>(state, state.gpr[6]))
    *output = volatile_size;
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
