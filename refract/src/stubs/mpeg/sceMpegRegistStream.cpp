void sceMpegRegistStream(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto engine = mpeg_state::engine_from_mpeg(state.gpr[4]);
  state.gpr[2] = engine == nullptr
                     ? 0U
                     : engine->register_stream(state.gpr[5], state.gpr[6]);
  static std::atomic<std::uint32_t> traced_registrations{};
  if (implementation.verbose &&
      traced_registrations.fetch_add(1U, std::memory_order_relaxed) < 16U) {
    const auto kind = engine == nullptr
                          ? std::nullopt
                          : engine->stream_kind(state.gpr[2]);
    const auto kind_name = !kind               ? "invalid"
                           : *kind == mpeg_state::StreamKind::video
                               ? "video"
                           : *kind == mpeg_state::StreamKind::audio ? "audio"
                                                                    : "other";
    std::fprintf(stderr,
                 "[psprism:mpeg] register mpeg=%08x type=%u channel=%u "
                 "uid=%u kind=%s engine=%u\n",
                 state.gpr[4], state.gpr[5], state.gpr[6], state.gpr[2],
                 kind_name, engine != nullptr ? 1U : 0U);
  }
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
