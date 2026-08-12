void sceMpegMallocAvcEsBuf(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  state.gpr[2] = 1U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
