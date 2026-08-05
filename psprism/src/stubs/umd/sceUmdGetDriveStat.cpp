void sceUmdGetDriveStat(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  state.gpr[2] = 0x32U; // present, ready, readable
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
