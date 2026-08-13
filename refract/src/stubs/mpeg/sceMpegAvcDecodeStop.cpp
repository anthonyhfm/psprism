void sceMpegAvcDecodeStop(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  auto* status =
      mpeg_state::guest_pointer<std::uint32_t>(state, state.gpr[7]);
  const auto engine = mpeg_state::engine_from_mpeg(state.gpr[4]);
  if (status == nullptr || engine == nullptr) {
    state.gpr[2] = mpeg_state::invalid_value;
    return;
  }
  *status = 0U;
  engine->flush();
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
