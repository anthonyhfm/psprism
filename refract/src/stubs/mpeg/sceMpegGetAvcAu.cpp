void sceMpegGetAvcAu(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  auto* ringbuffer = mpeg_state::ringbuffer_from_mpeg(state, state.gpr[4]);
  auto* access_unit =
      mpeg_state::guest_pointer<mpeg_state::AccessUnit>(state, state.gpr[6]);
  if (ringbuffer == nullptr || access_unit == nullptr) {
    state.gpr[2] = 0xffffffffU;
    return;
  }
  ringbuffer->packets_available = 0;
  access_unit->presentation_timestamp = 0;
  access_unit->decode_timestamp = -1;
  state.gpr[2] = mpeg_state::no_data;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
