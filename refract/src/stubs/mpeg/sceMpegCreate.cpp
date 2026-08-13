void sceMpegCreate(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  auto* mpeg = mpeg_state::guest_pointer<std::uint32_t>(state, state.gpr[4]);
  auto* ringbuffer =
      mpeg_state::guest_pointer<mpeg_state::Ringbuffer>(state, state.gpr[7]);
  const auto handle_address = state.gpr[5] + 0x30U;
  auto* handle = reinterpret_cast<std::uint8_t*>(
      psprecomp::mapped_address(state, handle_address, 24U));
  if (mpeg == nullptr || ringbuffer == nullptr || handle == nullptr ||
      state.gpr[6] < mpeg_state::required_memory_size) {
    state.gpr[2] = 0xffffffffU;
    return;
  }
  *mpeg = handle_address;
  std::memcpy(handle, "LIBMPEG\0" "001\0", 12U);
  auto* values = reinterpret_cast<std::uint32_t*>(handle + 12U);
  values[0] = 0xffffffffU;
  values[1] = state.gpr[7];
  values[2] = ringbuffer->data_upper_bound;
  ringbuffer->mpeg = state.gpr[4];
  const auto capacity = static_cast<std::size_t>(
      std::max(ringbuffer->packets, 1)) * mpeg_state::ringbuffer_packet_size;
  mpeg_state::create_media_engine(
      psprecomp::canonical_address(state.gpr[4]), capacity);
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
