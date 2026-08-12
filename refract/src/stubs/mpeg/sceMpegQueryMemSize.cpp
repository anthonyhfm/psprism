void sceMpegQueryMemSize(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  state.gpr[2] = mpeg_state::required_memory_size;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
