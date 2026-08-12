void sceMpegRingbufferPut(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  auto* ringbuffer =
      mpeg_state::guest_pointer<mpeg_state::Ringbuffer>(state, state.gpr[4]);
  if (ringbuffer == nullptr || ringbuffer->packets <= 0 ||
      ringbuffer->callback == 0U) {
    state.gpr[2] = 0xffffffffU;
    return;
  }
  auto requested = std::min(
      static_cast<std::int32_t>(state.gpr[5]),
      static_cast<std::int32_t>(state.gpr[6]));
  requested = std::min(requested,
                       ringbuffer->packets - ringbuffer->packets_available);
  std::int32_t total_added{};
  while (requested > 0) {
    const auto write_position =
        ringbuffer->packets_write_position % ringbuffer->packets;
    const auto contiguous =
        std::min(requested, ringbuffer->packets - write_position);
    std::uint32_t callback_result{};
    if (!dispatch_guest_callback(
            implementation, state, ringbuffer->callback,
            ringbuffer->data + static_cast<std::uint32_t>(write_position) *
                                   mpeg_state::ringbuffer_packet_size,
            static_cast<std::uint32_t>(contiguous),
            ringbuffer->callback_argument, &callback_result)) {
      state.gpr[2] = 0xffffffffU;
      return;
    }
    const auto added = std::clamp(
        static_cast<std::int32_t>(callback_result), 0, contiguous);
    ringbuffer->packets_read += added;
    ringbuffer->packets_write_position += added;
    ringbuffer->packets_available += added;
    total_added += added;
    requested -= added;
    if (added != contiguous) break;
  }
  state.gpr[2] = static_cast<std::uint32_t>(total_added);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
