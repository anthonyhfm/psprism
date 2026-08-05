void sceIoDevctl(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto* device = guest_string(state, state.gpr[4]);
  if (device != nullptr && std::string_view(device) == "fatms0:" &&
      state.gpr[5] == 0x02415821U) {
    if (const auto* uid = guest_pointer<std::uint32_t>(state, state.gpr[6]))
      dispatch_notify_callback(implementation, state, static_cast<int>(*uid), 1U);
    state.gpr[2] = 0U;
  }
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
