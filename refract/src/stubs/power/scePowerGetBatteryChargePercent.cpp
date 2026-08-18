void scePowerGetBatteryChargePercent(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  state.gpr[2] = 100U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
