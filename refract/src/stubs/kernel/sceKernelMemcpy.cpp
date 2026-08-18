#include <cstring>

void sceKernelMemcpy(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  const auto dest = state.gpr[4];
  const auto src = state.gpr[5];
  const auto count = state.gpr[6];
  if (auto* dest_ptr = psprecomp::mapped_address(state, dest, count)) {
    if (const auto* src_ptr = psprecomp::mapped_address(state, src, count)) {
      std::memcpy(dest_ptr, src_ptr, count);
    }
  }
  state.gpr[2] = dest;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
