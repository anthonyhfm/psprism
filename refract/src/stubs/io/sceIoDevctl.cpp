void sceIoDevctl(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto* device = guest_string(state, state.gpr[4]);
  if (device == nullptr) {
    state.gpr[2] = io_error;
    return;
  }
  const std::string_view device_name(device);
  if (io_state::equals_case_insensitive(device_name, "fatms0:") &&
      state.gpr[5] == 0x02415821U) {
    if (const auto* uid = guest_pointer<std::uint32_t>(state, state.gpr[6]))
      dispatch_notify_callback(implementation, state, static_cast<int>(*uid), 1U);
    state.gpr[2] = 0U;
    return;
  }
  if ((io_state::equals_case_insensitive(device_name, "ms0:") ||
       io_state::equals_case_insensitive(device_name, "fatms0:")) &&
      state.gpr[5] == 0x02425818U && state.gpr[7] >= 4U) {
    const auto* command = guest_pointer<std::uint32_t>(state, state.gpr[6]);
    if (command == nullptr) {
      state.gpr[2] = io_error;
      return;
    }
    auto* capacity = guest_pointer<devctl_state::DeviceCapacity>(state, *command);
    if (capacity == nullptr) {
      state.gpr[2] = io_error;
      return;
    }
    *capacity = devctl_state::memory_stick_capacity();
    state.gpr[2] = 0U;
    return;
  }
  state.gpr[2] = io_error;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
