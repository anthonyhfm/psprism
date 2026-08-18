void sceMpegGetAtracAu(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  auto* ringbuffer = mpeg_state::ringbuffer_from_mpeg(state, state.gpr[4]);
  auto* access_unit =
      mpeg_state::guest_pointer<mpeg_state::AccessUnit>(state, state.gpr[6]);
  const auto engine = mpeg_state::engine_from_mpeg(state.gpr[4]);
  if (ringbuffer == nullptr || access_unit == nullptr || engine == nullptr) {
    state.gpr[2] = mpeg_state::invalid_value;
    return;
  }
  const auto next = engine->next_access_unit(state.gpr[5]);
  if (!next) {
    const auto last_pts = engine->last_audio_pts();
    access_unit->presentation_timestamp = last_pts >= 0 ? last_pts : 0;
    access_unit->decode_timestamp = -1;
    state.gpr[2] = mpeg_state::no_data;
    return;
  }
  access_unit->presentation_timestamp = next->pts;
  access_unit->decode_timestamp = next->dts;
  access_unit->elementary_stream_buffer = state.gpr[5];
  access_unit->elementary_stream_size =
      static_cast<std::uint32_t>(next->bytes.size());
  if (auto* attribute =
          mpeg_state::guest_pointer<std::uint32_t>(state, state.gpr[7])) {
    *attribute = 0U;
  }
  mpeg_state::update_ringbuffer_usage(*ringbuffer, *engine);
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
