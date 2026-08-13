void sceGeRestoreContext(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto* input = psprecomp::mapped_address(
      state, state.gpr[4], ge::context_word_count * sizeof(std::uint32_t));
  if (input == nullptr) {
    state.gpr[2] = 0x80000103U;
    return;
  }
  std::array<std::uint32_t, ge::context_word_count> context{};
  std::memcpy(context.data(), input, sizeof(context));
  {
    std::lock_guard graphics_lock(implementation.graphics.mutex);
    if (implementation.ge_scheduler.busy()) {
      state.gpr[2] = 0x800201a7U;
      return;
    }
    implementation.graphics.restore(context);
  }
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
