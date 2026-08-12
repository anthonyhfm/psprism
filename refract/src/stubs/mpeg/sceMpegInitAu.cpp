void sceMpegInitAu(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  auto* access_unit =
      mpeg_state::guest_pointer<mpeg_state::AccessUnit>(state, state.gpr[6]);
  if (access_unit == nullptr) {
    state.gpr[2] = 0xffffffffU;
    return;
  }
  access_unit->presentation_timestamp = 0;
  access_unit->decode_timestamp = state.gpr[5] <= 2U ? 0 : -1;
  access_unit->elementary_stream_buffer = 0U;
  access_unit->elementary_stream_size =
      state.gpr[5] <= 2U ? mpeg_state::avc_es_size
                         : mpeg_state::atrac_es_size;
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
