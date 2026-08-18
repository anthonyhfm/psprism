void sceMpegRingbufferConstruct(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  auto* ringbuffer =
      mpeg_state::guest_pointer<mpeg_state::Ringbuffer>(state, state.gpr[4]);
  if (ringbuffer == nullptr) {
    state.gpr[2] = mpeg_state::invalid_address;
    return;
  }
  if (mpeg_state::ringbuffer_memory_size(state.gpr[5]) > state.gpr[7]) {
    state.gpr[2] = mpeg_state::insufficient_memory;
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
  const auto data_upper_bound = static_cast<std::uint64_t>(state.gpr[6]) +
                                static_cast<std::uint64_t>(state.gpr[5]) *
                                    mpeg_state::ringbuffer_packet_size;
  if (data_upper_bound > UINT32_MAX) {
    state.gpr[2] = mpeg_state::invalid_address;
    return;
  }
  ringbuffer->data_upper_bound = static_cast<std::uint32_t>(data_upper_bound);
  ringbuffer->mpeg = 0U;
  // The PSP ABI exposes an eleven-word (44-byte) ringbuffer.  Callback $gp is
  // kernel-owned context, not a twelfth guest word; writing it at +0x2c
  // corrupts objects which embed SceMpegRingbuffer followed by game data.
  mpeg_state::remember_ringbuffer_gp(
      psprecomp::canonical_address(state.gpr[4]), state.gpr[28]);
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
