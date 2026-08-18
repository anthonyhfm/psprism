#include <cstring>

void sceKernelMemset(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  const auto dest = state.gpr[4];
  const auto val = static_cast<std::uint8_t>(state.gpr[5]);
  const auto count = state.gpr[6];
  if (auto* output = psprecomp::mapped_address(state, dest, count)) {
    std::memset(output, val, count);
  }
  state.gpr[2] = dest;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
