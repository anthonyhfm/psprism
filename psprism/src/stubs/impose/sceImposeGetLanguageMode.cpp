void sceImposeGetLanguageMode(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  if (auto* language = guest_pointer<std::uint32_t>(state, state.gpr[4]))
    *language = 1U; // English
  if (auto* button = guest_pointer<std::uint32_t>(state, state.gpr[5]))
    *button = 1U; // Cross confirms
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
