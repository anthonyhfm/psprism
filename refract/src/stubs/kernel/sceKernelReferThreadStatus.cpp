#include "thread_status.hpp"

void sceKernelReferThreadStatus(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  static_cast<void>(implementation);
  constexpr std::size_t info_size = 0x68U;
  auto* output = psprecomp::mapped_address(state, state.gpr[5], info_size);
  if (output == nullptr) {
    state.gpr[2] = static_cast<std::uint32_t>(-1);
    return;
  }

  std::uint32_t requested_size{};
  std::memcpy(&requested_size, output, sizeof(requested_size));
  std::array<std::uint8_t, info_size> info{};
  const auto snapshot =
      thread_status::get(static_cast<int>(state.gpr[4]));
  const auto copy_field = [&](std::size_t offset, const auto& value) {
    std::memcpy(info.data() + offset, &value, sizeof(value));
  };
  copy_field(0x00U, requested_size);
  copy_field(0x28U, snapshot.status);
  copy_field(0x44U, snapshot.wait_type);
  copy_field(0x48U, snapshot.wait_id);
  std::memcpy(output, info.data(), std::min<std::size_t>(requested_size, info.size()));
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
