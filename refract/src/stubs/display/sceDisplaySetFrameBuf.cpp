void sceDisplaySetFrameBuf(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  constexpr std::uint32_t width = 480;
  constexpr std::uint32_t height = 272;
  const auto bytes_per_pixel = state.gpr[6] == 3U ? 4U : 2U;
  const auto source_stride = std::max<std::uint32_t>(state.gpr[5], width);
  const auto requested_byte_count =
      static_cast<std::size_t>(source_stride) * height * bytes_per_pixel;
  auto* pixels = psprecomp::mapped_address(state, state.gpr[4], requested_byte_count);
  if (pixels == nullptr) {
    const auto fallback_width = static_cast<std::size_t>(width);
    const auto fallback_byte_count = fallback_width * height * bytes_per_pixel;
    pixels = psprecomp::mapped_address(state, state.gpr[4], fallback_byte_count);
  }
  if (pixels != nullptr) {
    const auto frame = implementation.displayed_frames++;
    if (implementation.verbose && frame < 4U) {
      std::fprintf(stderr,
                   "[psprism:display] frame=%u address=%08x stride=%u "
                   "format=%u sync=%u\n",
                   frame, state.gpr[4], state.gpr[5], state.gpr[6],
                   state.gpr[7]);
    }
    host::present_frame(pixels, source_stride, width, height, state.gpr[6],
                        state.gpr[4]);
  } else {
    if (implementation.verbose)
      std::fprintf(stderr, "[psprism:display] setframebuf unmapped=%08x\n",
                   state.gpr[4]);
  }
  state.gpr[2] = (pixels == nullptr) ? 0x80000103U : 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
