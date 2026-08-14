void sceMpegRingbufferPut(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  static std::atomic<std::uint32_t> traced_callbacks{};
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
  const auto engine = mpeg_state::engine_from_mpeg(ringbuffer->mpeg);
  if (engine == nullptr) {
    state.gpr[2] = mpeg_state::invalid_value;
    return;
  }
  while (requested > 0) {
    const auto write_position =
        ringbuffer->packets_write_position % ringbuffer->packets;
    const auto contiguous =
        std::min(requested, ringbuffer->packets - write_position);
    std::uint32_t callback_result{};
    const auto data_address =
        ringbuffer->data + static_cast<std::uint32_t>(write_position) *
                               mpeg_state::ringbuffer_packet_size;
    const auto trace_index =
        traced_callbacks.fetch_add(1U, std::memory_order_relaxed);
    if (implementation.verbose && trace_index < 16U) {
      std::fprintf(stderr,
                   "[psprism:mpeg] ring-put callback=%08x gp=%08x "
                   "data=%08x packets=%d\n",
                   ringbuffer->callback, ringbuffer->gp, data_address,
                   contiguous);
    }
    if (!dispatch_guest_callback(
            implementation, state, ringbuffer->callback,
            data_address,
            static_cast<std::uint32_t>(contiguous),
            ringbuffer->callback_argument, &callback_result,
            ringbuffer->gp)) {
      state.gpr[2] = 0xffffffffU;
      return;
    }
    const auto added = std::clamp(
        static_cast<std::int32_t>(callback_result), 0, contiguous);
    if (implementation.verbose && trace_index < 16U) {
      std::fprintf(stderr,
                   "[psprism:mpeg] ring-put result=%08x added=%d/%d\n",
                   callback_result, added, contiguous);
    }
    const auto byte_count = static_cast<std::size_t>(added) *
                            mpeg_state::ringbuffer_packet_size;
    const auto* bytes = psprecomp::mapped_address(state, data_address,
                                                   byte_count);
    const auto append_ok =
        (byte_count == 0U || bytes != nullptr) &&
        engine->append_packets(std::span<const std::uint8_t>(bytes, byte_count));
    if (implementation.verbose && trace_index < 64U) {
      const auto stats = engine->queue_stats();
      std::fprintf(stderr,
                   "[psprism:mpeg] ring-append bytes=%zu ok=%u "
                   "units(v/a/o)=%zu/%zu/%zu staged=%zu/%zu "
                   "pending=%u/%u capacity=%zu\n",
                   byte_count, append_ok ? 1U : 0U, stats.video_units,
                   stats.audio_units, stats.other_units, stats.encoded_bytes,
                   stats.audio_staging_bytes, stats.pending_video ? 1U : 0U,
                   stats.pending_audio ? 1U : 0U, stats.capacity_bytes);
    }
    if (!append_ok) {
      break;
    }
    ringbuffer->packets_read += added;
    ringbuffer->packets_write_position += added;
    total_added += added;
    requested -= added;
    if (added != contiguous) break;
  }
  mpeg_state::update_ringbuffer_usage(*ringbuffer, *engine);
  state.gpr[2] = static_cast<std::uint32_t>(total_added);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
