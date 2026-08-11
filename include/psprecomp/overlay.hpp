#pragma once

#include <cstddef>
#include <cstdint>

namespace psprecomp {

// Register frame shared by the generated PSP overlay trampoline and its C++
// runner. Keep the offsets in sync with the static assertions: Allegrex
// assembly saves the complete user-visible CPU/FPU/VFPU state here before
// entering compiler-generated code.
struct alignas(16) PspOverlayContext {
    std::uint32_t gpr[32]{};
    std::uint32_t hi{};
    std::uint32_t lo{};
    std::uint32_t fpr[32]{};
    std::uint32_t fcr31{};
    // Set by the route stub. Threads without VFPU code must not execute VFPU
    // save/restore instructions merely to cross an integer-only overlay.
    std::uint32_t vfpu_active{};
    // Physical C<matrix><column>0 quad order. The C++ bridge converts this to
    // State::vfpu's scalar-register indexing before running translated code.
    std::uint32_t vfpu[128]{};
    std::uint32_t vfpu_ctrl[16]{};
    std::uint32_t pc{};
    std::uint32_t tail_padding[3]{};
};

static_assert(offsetof(PspOverlayContext, hi) == 128U);
static_assert(offsetof(PspOverlayContext, lo) == 132U);
static_assert(offsetof(PspOverlayContext, fpr) == 136U);
static_assert(offsetof(PspOverlayContext, fcr31) == 264U);
static_assert(offsetof(PspOverlayContext, vfpu_active) == 268U);
static_assert(offsetof(PspOverlayContext, vfpu) == 272U);
static_assert(offsetof(PspOverlayContext, vfpu_ctrl) == 784U);
static_assert(offsetof(PspOverlayContext, pc) == 848U);
static_assert(sizeof(PspOverlayContext) == 864U);

} // namespace psprecomp
