void sceMpegGetAvcAu(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  static std::atomic<std::uint32_t> traced_access_units{};
  auto* ringbuffer = mpeg_state::ringbuffer_from_mpeg(state, state.gpr[4]);
  auto* access_unit =
      mpeg_state::guest_pointer<mpeg_state::AccessUnit>(state, state.gpr[6]);
  const auto engine = mpeg_state::engine_from_mpeg(state.gpr[4]);
  if (ringbuffer == nullptr || access_unit == nullptr || engine == nullptr) {
    state.gpr[2] = mpeg_state::invalid_value;
    return;
  }
  const auto next = engine->next_access_unit(state.gpr[5]);
  if (!next) {
    access_unit->presentation_timestamp = 0;
    access_unit->decode_timestamp = -1;
    state.gpr[2] = mpeg_state::no_data;
    const auto trace_index =
        traced_access_units.fetch_add(1U, std::memory_order_relaxed);
    if (implementation.verbose && trace_index < 64U) {
      const auto kind = engine->stream_kind(state.gpr[5]);
      const auto stats = engine->queue_stats();
      std::fprintf(stderr,
                   "[psprism:mpeg] avc-au uid=%u kind=%d no-data "
                   "units(v/a/o)=%zu/%zu/%zu staged=%zu/%zu "
                   "pending=%u/%u buffered=%zu packets=%d/%d\n",
                   state.gpr[5], kind ? static_cast<int>(*kind) : -1,
                   stats.video_units, stats.audio_units, stats.other_units,
                   stats.encoded_bytes, stats.audio_staging_bytes,
                   stats.pending_video ? 1U : 0U,
                   stats.pending_audio ? 1U : 0U, engine->buffered_bytes(),
                   ringbuffer->packets_available, ringbuffer->packets);
    }
    return;
  }
  access_unit->presentation_timestamp = next->pts;
  access_unit->decode_timestamp = next->dts;
  access_unit->elementary_stream_buffer = state.gpr[5];
  access_unit->elementary_stream_size =
      static_cast<std::uint32_t>(next->bytes.size());
  if (auto* attribute =
          mpeg_state::guest_pointer<std::uint32_t>(state, state.gpr[7])) {
    *attribute = 1U;
  }
  mpeg_state::update_ringbuffer_usage(*ringbuffer, *engine);
  const auto trace_index =
      traced_access_units.fetch_add(1U, std::memory_order_relaxed);
  if (implementation.verbose && trace_index < 64U) {
    const auto kind = engine->stream_kind(state.gpr[5]);
    const auto stats = engine->queue_stats();
    std::fprintf(stderr,
                 "[psprism:mpeg] avc-au uid=%u kind=%d bytes=%zu pts=%lld "
                 "units(v/a/o)=%zu/%zu/%zu pending=%u/%u buffered=%zu "
                 "packets=%d/%d\n",
                 state.gpr[5], kind ? static_cast<int>(*kind) : -1,
                 next->bytes.size(), static_cast<long long>(next->pts),
                 stats.video_units, stats.audio_units, stats.other_units,
                 stats.pending_video ? 1U : 0U,
                 stats.pending_audio ? 1U : 0U, engine->buffered_bytes(),
                 ringbuffer->packets_available, ringbuffer->packets);
  }
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
