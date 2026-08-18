void sceUtilityGetSystemParamInt(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  std::uint32_t value{};
  switch (state.gpr[4]) {
  case 2U: value = 0U; break;
  case 3U: value = 1U; break;
  case 4U: value = 2U; break;
  case 5U: value = 0U; break;
  case 6U: value = 0U; break;
  case 7U: value = 0U; break;
  case 8U: value = 1U; break;
  case 9U: value = 1U; break;
  case 10U: value = 0U; break;
  default:
    state.gpr[2] = 0x80110103U;
    return;
  }
  if (auto* output = guest_pointer<std::uint32_t>(state, state.gpr[5])) {
    *output = value;
    state.gpr[2] = 0U;
  }
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
