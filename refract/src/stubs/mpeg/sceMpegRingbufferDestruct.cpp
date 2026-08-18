void sceMpegRingbufferDestruct(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  mpeg_state::forget_ringbuffer_gp(
      psprecomp::canonical_address(state.gpr[4]));
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
