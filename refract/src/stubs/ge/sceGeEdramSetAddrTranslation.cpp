void sceGeEdramSetAddrTranslation(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto width = state.gpr[4];
  if (width != 0U &&
      (width < 0x200U || width > 0x1000U ||
       (width & (width - 1U)) != 0U)) {
    state.gpr[2] = 0x800001feU;
    return;
  }
  std::lock_guard graphics_lock(implementation.graphics.mutex);
  state.gpr[2] = implementation.graphics.address_translation_width;
  implementation.graphics.address_translation_width = width;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
