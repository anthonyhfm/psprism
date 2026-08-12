void sceMpegRingbufferAvailableSize(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  const auto* ringbuffer =
      mpeg_state::guest_pointer<mpeg_state::Ringbuffer>(state, state.gpr[4]);
  if (ringbuffer == nullptr || ringbuffer->packets < 0 ||
      ringbuffer->packets_available < 0 ||
      ringbuffer->packets_available > ringbuffer->packets) {
    state.gpr[2] = 0xffffffffU;
    return;
  }
  state.gpr[2] = static_cast<std::uint32_t>(
      ringbuffer->packets - ringbuffer->packets_available);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
