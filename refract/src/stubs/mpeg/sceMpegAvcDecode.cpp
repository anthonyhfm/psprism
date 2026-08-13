void sceMpegAvcDecode(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto engine = mpeg_state::engine_from_mpeg(state.gpr[4]);
  const auto* access_unit =
      mpeg_state::guest_pointer<mpeg_state::AccessUnit>(state, state.gpr[5]);
  const auto* output_address =
      mpeg_state::guest_pointer<std::uint32_t>(state, state.gpr[7]);
  auto* initialized =
      mpeg_state::guest_pointer<std::uint32_t>(state, state.gpr[8]);
  const auto stride = state.gpr[6];
  if (engine == nullptr || access_unit == nullptr || output_address == nullptr ||
      initialized == nullptr || stride == 0U) {
    state.gpr[2] = mpeg_state::invalid_value;
    return;
  }
  const auto pixel_format = engine->video_pixel_format();
  const auto bytes_per_pixel = pixel_format == 3U ? 4U : 2U;
  constexpr std::uint32_t maximum_height = 272U;
  const auto output_size = static_cast<std::size_t>(stride) * maximum_height *
                           bytes_per_pixel;
  auto* output =
      psprecomp::mapped_address(state, *output_address, output_size);
  if (output == nullptr) {
    state.gpr[2] = mpeg_state::invalid_address;
    return;
  }
  mpeg_state::VideoFrameInfo frame;
  bool decoded{};
  {
    GuestExecutionPause pause(implementation);
    decoded = engine->decode_pending_video(
        std::span<std::uint8_t>(output, output_size), stride, pixel_format,
        frame);
  }
  if (!decoded) {
    *initialized = 0U;
    state.gpr[2] = engine->ffmpeg_available() ? mpeg_state::decode_fatal
                                              : unimplemented;
    return;
  }
  *initialized = frame.produced ? 1U : 0U;
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
