void sceMpegDelete(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  mpeg_state::delete_media_engine(
      psprecomp::canonical_address(state.gpr[4]));
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
