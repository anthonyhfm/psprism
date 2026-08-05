void sceDmacMemcpy(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  const auto dest = state.gpr[4];
  const auto src = state.gpr[5];
  const auto size = state.gpr[6];
  auto* dest_ptr = psprecomp::mapped_address(state, dest, size);
  const auto* src_ptr = psprecomp::mapped_address(state, src, size);
  if (dest_ptr && src_ptr && size > 0) {
    std::memcpy(dest_ptr, src_ptr, size);
  }
  state.gpr[2] = 0;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
