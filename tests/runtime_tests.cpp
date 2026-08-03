#include <psprecomp/runtime.hpp>
#include <psprecomp/relocation.hpp>
#include <psprecomp/vfpu.hpp>

#include <array>

#define CHECK(expression)                                                        \
    do {                                                                         \
        if (!(expression)) {                                                     \
            return __LINE__;                                                     \
        }                                                                        \
    } while (false)

int main() {
    std::array<std::uint8_t, 16> memory{};
    psprecomp::State state;
    state.memory = memory.data();
    state.memory_size = memory.size();
    state.memory_base = 0x1000;

    psprecomp::store32(state, 0x1004, 0x89abcdefU);
    CHECK(state.stop_reason == psprecomp::StopReason::running);
    CHECK(psprecomp::load32(state, 0x1004) == 0x89abcdefU);
    CHECK(memory[4] == 0xef && memory[7] == 0x89);

    (void)psprecomp::load16(state, 0x1001);
    CHECK(state.stop_reason == psprecomp::StopReason::memory_fault);
    CHECK(state.fault_address == 0x1001);
    CHECK(psprecomp::arithmetic_shift_right(0x80000000U, 1) == 0xc0000000U);
    CHECK(psprecomp::arithmetic_shift_right(0x7fffffffU, 4) == 0x07ffffffU);
    CHECK(psprecomp::rotate_right(0x12345678U, 8) == 0x78123456U);
    psprecomp::set_f32(state, 3, 1.5F);
    CHECK(psprecomp::f32(state, 3) == 1.5F);
    CHECK(psprecomp::rounded_word(1.6F, 0) == 2U);
    CHECK(psprecomp::rounded_word(-1.6F, 1) == 0xffffffffU);
    psprecomp::compare_f32(state, 0, 1.0F, 2.0F, 0x0c);
    CHECK(psprecomp::fpu_condition(state, 0));

    state.stop_reason = psprecomp::StopReason::running;
    psprecomp::store_word_right(state, 0x1002, 0x44332211U);
    psprecomp::store_word_left(state, 0x1005, 0x44332211U);
    CHECK(memory[2] == 0x11 && memory[3] == 0x22);
    CHECK(memory[4] == 0x33 && memory[5] == 0x44);

    state.vfpu[psprecomp::vfpu_index(0, 1)] =
        std::bit_cast<std::uint32_t>(2.0F);
    state.vfpu[psprecomp::vfpu_index(1, 1)] =
        std::bit_cast<std::uint32_t>(3.0F);
    CHECK(psprecomp::vfpu_float(state, psprecomp::vfpu_index(0, 1)) == 2.0F);

    // VFPU scalar encodings are 0XXMMMYY.  A non-transposed M000 column
    // therefore walks 0x00, 0x20, 0x40, 0x60 rather than four adjacent
    // scalar encodings.
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            const auto index = static_cast<std::uint32_t>(column + row * 32);
            state.vfpu[index] = std::bit_cast<std::uint32_t>(
                static_cast<float>(column * 10 + row));
        }
    }
    float matrix[16]{};
    psprecomp::vfpu_read_matrix(state, 0, 4, matrix);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            CHECK(matrix[column * 4 + row] ==
                  static_cast<float>(column * 10 + row));
        }
    }
    float transposed[16]{};
    psprecomp::vfpu_read_matrix(state, 0x20, 4, transposed);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            CHECK(transposed[column * 4 + row] ==
                  static_cast<float>(row * 10 + column));
        }
    }

    std::array<std::uint8_t, 32> relocated{};
    const psprecomp::PspLoadSegment segments[] = {{0, relocated.size(), 0}};
    const psprecomp::PspRelocationRecord relocations[] = {
        {0, 2, 0, 0, 0},
        {4, 4, 0, 0, 0},
        {8, 5, 0, 0, 0},
        {12, 6, 0, 0, 0},
    };
    psprecomp::detail::write_u32(relocated.data(), 0, 0x00000020U);
    psprecomp::detail::write_u32(relocated.data(), 4, 0x08000010U);
    psprecomp::detail::write_u32(relocated.data(), 8, 0x3c080001U);
    psprecomp::detail::write_u32(relocated.data(), 12, 0x25088000U);
    CHECK(psprecomp::apply_psp_relocations(
              relocated.data(), relocated.size(), 0x08800000U, segments, 1,
              relocations, 4) == psprecomp::RelocationResult::success);
    CHECK(psprecomp::detail::read_u32(relocated.data(), 0) == 0x08800020U);
    CHECK(psprecomp::detail::read_u32(relocated.data(), 4) == 0x0a200010U);
    CHECK(psprecomp::detail::read_u32(relocated.data(), 8) == 0x3c080881U);
    CHECK(psprecomp::detail::read_u32(relocated.data(), 12) == 0x25088000U);
}
