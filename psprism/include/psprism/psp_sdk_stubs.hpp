#pragma once

#include <psprecomp/runtime.hpp>

namespace psprism::pspsdk {

#define PSPSDK_STUB(name) void name(psprecomp::State& state);

#include <psprism/psp_sdk_stubs.inc>

#undef PSPSDK_STUB

} // namespace psprism::pspsdk
