void sceCtrlPeekBufferPositive(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
      constexpr std::size_t pad_size = 16;
      const auto count = state.gpr[5];
      if (!psprecomp::address_ok(state, state.gpr[4], pad_size * count))
        return;
      auto* output =
          psprecomp::mapped_address(state, state.gpr[4], pad_size * count);
      const auto timestamp =
          static_cast<std::uint64_t>(implementation.elapsed_microseconds());
      const auto controller = host::controller_state();
      std::memset(output, 0, pad_size * count);
      for (std::uint32_t index = 0; index < count; ++index) {
        const auto sample_timestamp = static_cast<std::uint32_t>(timestamp);
        std::memcpy(output + index * pad_size, &sample_timestamp,
                    sizeof(sample_timestamp));
        std::memcpy(output + index * pad_size + 4U, &controller.buttons,
                    sizeof(controller.buttons));
        output[index * pad_size + 8] = controller.analog_x;
        output[index * pad_size + 9] = controller.analog_y;
      }
      state.gpr[2] = count;
      return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
