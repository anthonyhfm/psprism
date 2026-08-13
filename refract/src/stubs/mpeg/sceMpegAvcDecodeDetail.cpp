void sceMpegAvcDecodeDetail(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  const auto engine = mpeg_state::engine_from_mpeg(state.gpr[4]);
  auto* detail = reinterpret_cast<std::uint32_t*>(
      psprecomp::mapped_address(state, state.gpr[5], 9U * sizeof(std::uint32_t)));
  if (engine == nullptr || detail == nullptr) {
    state.gpr[2] = mpeg_state::invalid_value;
    return;
  }
  const auto frame = engine->last_video_frame();
  detail[0] = 0U;
  detail[1] = frame.frame_number;
  detail[2] = frame.width;
  detail[3] = frame.height;
  detail[4] = 0U;
  detail[5] = 0U;
  detail[6] = 0U;
  detail[7] = 0U;
  detail[8] = frame.produced ? 1U : 0U;
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
