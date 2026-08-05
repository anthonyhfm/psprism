#include <refract/psp_sdk_stubs.hpp>

#include <cstddef>
#include <cstdint>

namespace {

inline std::uint32_t stack_argument(const psprecomp::State& state,
                                   std::size_t index) {
  const auto address =
      psprecomp::canonical_address(state.gpr[29] + 16U + (index * 4U));
  if (const auto* pointer = psprecomp::mapped_address(state, address, 4U)) {
    return static_cast<std::uint32_t>(pointer[0]) |
           (static_cast<std::uint32_t>(pointer[1]) << 8U) |
           (static_cast<std::uint32_t>(pointer[2]) << 16U) |
           (static_cast<std::uint32_t>(pointer[3]) << 24U);
  }
  if (!state.direct_memory_access || (address & 3U) != 0U ||
      address < 0x00010000U) {
    return 0U;
  }
  const auto* bytes = reinterpret_cast<const volatile std::uint8_t*>(
      static_cast<std::uintptr_t>(address));
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

using PspSdkFunction = std::uint32_t (*)(...);

inline std::uint32_t call_sdk_api(PspSdkFunction function,
                                 const psprecomp::State& state) {
  if (reinterpret_cast<void*>(function) == nullptr) {
    return 0x80000001U;
  }
  return function(
      state.gpr[4], state.gpr[5], state.gpr[6], state.gpr[7],
      stack_argument(state, 0), stack_argument(state, 1),
      stack_argument(state, 2), stack_argument(state, 3),
      stack_argument(state, 4), stack_argument(state, 5),
      stack_argument(state, 6), stack_argument(state, 7),
      stack_argument(state, 8), stack_argument(state, 9),
      stack_argument(state, 10), stack_argument(state, 11));
}

} // namespace

// Declare all symbols as variadic weak symbols to support generic forwarding
// without requiring per-function signatures or forcing links on missing PSPSDK stubs.
#define PSPSDK_STUB(name) \
  extern "C" __attribute__((weak)) std::uint32_t name(...);
#include <refract/psp_sdk_stubs.inc>
#undef PSPSDK_STUB

namespace refract::pspsdk {
#define PSPSDK_STUB(name)                                                     \
  void name(psprecomp::State& state) {                                       \
    state.gpr[2] = call_sdk_api(::name, state);                             \
  }
#include <refract/psp_sdk_stubs.inc>
#undef PSPSDK_STUB
} // namespace refract::pspsdk
