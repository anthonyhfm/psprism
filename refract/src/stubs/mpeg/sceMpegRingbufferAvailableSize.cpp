void sceMpegRingbufferAvailableSize(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  (void)state;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
