void sceMpegAvcDecodeMode(Implementation& implementation,
                          psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  const auto* mode =
      mpeg_state::guest_pointer<std::int32_t>(state, state.gpr[5]);
  const auto engine = mpeg_state::engine_from_mpeg(state.gpr[4]);
  if (mode == nullptr || engine == nullptr || mode[0] < -1 || mode[0] > 2 ||
      mode[1] < -1 || mode[1] > 3 ||
      (mode[0] != -1 && !engine->set_decode_mode(mode[0])) ||
      (mode[1] != -1 &&
       !engine->set_video_mode(static_cast<std::uint32_t>(mode[1])))) {
    state.gpr[2] = mpeg_state::invalid_value;
    return;
  }
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
