void sceMpegQueryAtracEsSize(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  auto* es_size = guest_pointer<std::uint32_t>(state, state.gpr[5]);
  auto* output_size = guest_pointer<std::uint32_t>(state, state.gpr[6]);
  if (es_size == nullptr || output_size == nullptr) {
    state.gpr[2] = 0xffffffffU;
    return;
  }
  *es_size = mpeg_state::atrac_es_size;
  *output_size = mpeg_state::atrac_es_output_size;
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
