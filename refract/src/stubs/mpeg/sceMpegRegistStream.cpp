void sceMpegRegistStream(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  const auto engine = mpeg_state::engine_from_mpeg(state.gpr[4]);
  state.gpr[2] = engine == nullptr
                     ? 0U
                     : engine->register_stream(state.gpr[5], state.gpr[6]);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
