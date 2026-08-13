void sceMpegQueryStreamOffset(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  const auto* header = psprecomp::mapped_address(state, state.gpr[5], 144U);
  auto* output = mpeg_state::guest_pointer<std::uint32_t>(state, state.gpr[6]);
  if (header == nullptr || output == nullptr) {
    state.gpr[2] = mpeg_state::invalid_address;
    return;
  }
  const auto parsed = mpeg_state::parse_psmf_header(
      std::span<const std::uint8_t>(header, 144U));
  if (!parsed) {
    *output = 0U;
    state.gpr[2] = mpeg_state::invalid_value;
    return;
  }
  *output = parsed->stream_offset;
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
