void sceMpegFlushAllStream(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  if (auto* ringbuffer =
          mpeg_state::ringbuffer_from_mpeg(state, state.gpr[4])) {
    ringbuffer->packets_read = 0;
    ringbuffer->packets_write_position = 0;
    ringbuffer->packets_available = 0;
  }
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
