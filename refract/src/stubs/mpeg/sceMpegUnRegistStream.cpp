void sceMpegUnRegistStream(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  const auto engine = mpeg_state::engine_from_mpeg(state.gpr[4]);
  state.gpr[2] = engine != nullptr && engine->unregister_stream(state.gpr[5])
                     ? 0U
                     : mpeg_state::invalid_value;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
