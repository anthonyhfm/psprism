#pragma once

#include <psprecomp/runtime.hpp>

namespace refract::pspsdk {

#define PSPSDK_STUB(name) void name(psprecomp::State& state);

#include <refract/psp_sdk_stubs.inc>

#undef PSPSDK_STUB

} // namespace refract::pspsdk
