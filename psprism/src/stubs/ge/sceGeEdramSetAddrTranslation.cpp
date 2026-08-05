void sceGeEdramSetAddrTranslation(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  state.gpr[2] = state.gpr[4];
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
