void sceMpegRingbufferConstruct(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  auto* ringbuffer =
      mpeg_state::guest_pointer<mpeg_state::Ringbuffer>(state, state.gpr[4]);
  if (ringbuffer == nullptr || state.gpr[5] == 0U ||
      mpeg_state::ringbuffer_memory_size(state.gpr[5]) > state.gpr[7]) {
    state.gpr[2] = 0xffffffffU;
    return;
  }
  ringbuffer->packets = static_cast<std::int32_t>(state.gpr[5]);
  ringbuffer->packets_read = 0;
  ringbuffer->packets_write_position = 0;
  ringbuffer->packets_available = 0;
  ringbuffer->packet_size =
      static_cast<std::int32_t>(mpeg_state::ringbuffer_packet_size);
  ringbuffer->data = state.gpr[6];
  ringbuffer->callback = state.gpr[8];
  ringbuffer->callback_argument = state.gpr[9];
  ringbuffer->data_upper_bound =
      state.gpr[6] + state.gpr[5] * mpeg_state::ringbuffer_packet_size;
  ringbuffer->mpeg = 0U;
  ringbuffer->gp = state.gpr[28];
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
