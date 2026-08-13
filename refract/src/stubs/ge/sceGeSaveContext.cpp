void sceGeSaveContext(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  auto* output = psprecomp::mapped_address(
      state, state.gpr[4], ge::context_word_count * sizeof(std::uint32_t));
  if (output == nullptr) {
    state.gpr[2] = 0x80000103U;
    return;
  }
  std::array<std::uint32_t, ge::context_word_count> context{};
  {
    std::lock_guard graphics_lock(implementation.graphics.mutex);
    if (implementation.ge_scheduler.busy()) {
      state.gpr[2] = 0xffffffffU;
      return;
    }
    implementation.graphics.save(context);
  }
  std::memcpy(output, context.data(), sizeof(context));
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
