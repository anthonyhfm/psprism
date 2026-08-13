void sceMpegAtracDecode(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto engine = mpeg_state::engine_from_mpeg(state.gpr[4]);
  auto* output = reinterpret_cast<std::int16_t*>(
      psprecomp::mapped_address(state, state.gpr[6],
                                mpeg_state::atrac_es_output_size));
  if (engine == nullptr || output == nullptr) {
    state.gpr[2] = mpeg_state::invalid_address;
    return;
  }
  std::memset(output, 0, mpeg_state::atrac_es_output_size);
  std::uint32_t samples{};
  bool decoded{};
  {
    GuestExecutionPause pause(implementation);
    decoded = engine->decode_pending_audio(
        std::span<std::int16_t>(output,
                                mpeg_state::atrac_es_output_size /
                                    sizeof(std::int16_t)),
        samples);
  }
  state.gpr[2] = decoded ? 0U : mpeg_state::decode_fatal;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
