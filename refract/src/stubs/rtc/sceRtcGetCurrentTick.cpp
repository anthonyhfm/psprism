void sceRtcGetCurrentTick(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  constexpr std::uint64_t unix_epoch_psp_ticks = 62135596800000000ULL;
  if (auto* output = guest_pointer<std::uint64_t>(state, state.gpr[4])) {
    *output = unix_epoch_psp_ticks +
              implementation.start_unix_seconds * 1000000ULL +
              implementation.elapsed_microseconds();
    state.gpr[2] = 0U;
  }
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
