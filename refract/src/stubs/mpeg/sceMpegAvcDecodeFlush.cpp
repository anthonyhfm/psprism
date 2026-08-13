void sceMpegAvcDecodeFlush(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  if (const auto engine = mpeg_state::engine_from_mpeg(state.gpr[4])) {
    engine->flush();
    state.gpr[2] = 0U;
  } else {
    state.gpr[2] = mpeg_state::invalid_value;
  }
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
