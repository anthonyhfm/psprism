void sceMpegFlushAllStream(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  if (auto* ringbuffer =
          mpeg_state::ringbuffer_from_mpeg(state, state.gpr[4])) {
    ringbuffer->packets_read = 0;
    ringbuffer->packets_write_position = 0;
    ringbuffer->packets_available = 0;
  }
  if (auto engine = mpeg_state::engine_from_mpeg(state.gpr[4])) {
    engine->flush();
  }
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
