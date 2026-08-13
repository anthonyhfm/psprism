#include <psprecomp/overlay.hpp>
#include <psprecomp/allegrex_decoder.hpp>
#include <psprecomp/interpreter.hpp>
#include <psprecomp/relocation.hpp>
#include <psprecomp/runtime.hpp>
#include <psprecomp/vfpu.hpp>

#include <array>

#define CHECK(expression)                                                      \
    do {                                                                       \
        if (!(expression)) {                                                   \
            return __LINE__;                                                   \
        }                                                                      \
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
    CHECK(psprecomp::load32(state, 0x40001004U) == 0x89abcdefU);
    psprecomp::store32(state, 0x80001008U, 0x12345678U);
    CHECK(psprecomp::load32(state, 0xa0001008U) == 0x12345678U);
    CHECK(memory[4] == 0xef && memory[7] == 0x89);

    std::array<std::uint8_t, 16> volatile_memory{};
    state.volatile_memory = volatile_memory.data();
    state.volatile_memory_size = volatile_memory.size();
    psprecomp::store32(state, 0x08400004U, 0x10203040U);
    CHECK(state.stop_reason == psprecomp::StopReason::running);
    CHECK(psprecomp::load32(state, 0x08400004U) == 0x10203040U);
    CHECK(volatile_memory[4] == 0x40 && volatile_memory[7] == 0x10);

    std::array<std::uint8_t, 8> allegrex_bit_count_code{};
    psprecomp::State bit_count_state;
    bit_count_state.memory = allegrex_bit_count_code.data();
    bit_count_state.memory_size = allegrex_bit_count_code.size();
    bit_count_state.memory_base = 0x2000U;
    bit_count_state.pc = bit_count_state.memory_base;
    psprecomp::store32(bit_count_state, 0x2000U, 0x01402816U);
    psprecomp::store32(bit_count_state, 0x2004U, 0x01403017U);
    bit_count_state.gpr[10] = 0x00f0ffffU;
    CHECK(psprecomp::interpret_allegrex(bit_count_state, 0x2000U));
    CHECK(bit_count_state.gpr[5] == 8U);
    CHECK(psprecomp::interpret_allegrex(bit_count_state, 0x2004U));
    CHECK(bit_count_state.gpr[6] == 0U);

    std::array<std::uint8_t, 8> allegrex_min_max_code{};
    psprecomp::State min_max_state;
    min_max_state.memory = allegrex_min_max_code.data();
    min_max_state.memory_size = allegrex_min_max_code.size();
    min_max_state.memory_base = 0x3000U;
    min_max_state.pc = min_max_state.memory_base;
    constexpr auto max_r5_r10_r11 =
        (10U << 21U) | (11U << 16U) | (5U << 11U) | 0x2cU;
    constexpr auto min_r6_r10_r11 =
        (10U << 21U) | (11U << 16U) | (6U << 11U) | 0x2dU;
    psprecomp::store32(min_max_state, 0x3000U, max_r5_r10_r11);
    psprecomp::store32(min_max_state, 0x3004U, min_r6_r10_r11);
    min_max_state.gpr[10] = static_cast<std::uint32_t>(-4);
    min_max_state.gpr[11] = 7U;
    CHECK(psprecomp::interpret_allegrex(min_max_state, 0x3000U));
    CHECK(min_max_state.gpr[5] == 7U);
    CHECK(psprecomp::interpret_allegrex(min_max_state, 0x3004U));
    CHECK(min_max_state.gpr[6] == static_cast<std::uint32_t>(-4));

    std::array<std::uint8_t, 12> rotate_code{};
    psprecomp::State rotate_state;
    rotate_state.memory = rotate_code.data();
    rotate_state.memory_size = rotate_code.size();
    rotate_state.memory_base = 0x3800U;
    rotate_state.pc = rotate_state.memory_base;
    constexpr auto rotr_r5_r10_8 =
        (1U << 21U) | (10U << 16U) | (5U << 11U) | (8U << 6U) | 0x02U;
    constexpr auto rotrv_r6_r10_r11 =
        (11U << 21U) | (10U << 16U) | (6U << 11U) | (1U << 6U) | 0x06U;
    constexpr auto reserved_srl =
        (2U << 21U) | (10U << 16U) | (7U << 11U) | (4U << 6U) | 0x02U;
    static_assert(psprecomp::decode_allegrex(rotr_r5_r10_8).name == "rotr");
    static_assert(psprecomp::decode_allegrex(rotrv_r6_r10_r11).name ==
                  "rotrv");
    static_assert(!psprecomp::decode_allegrex(reserved_srl).valid());
    psprecomp::store32(rotate_state, 0x3800U, rotr_r5_r10_8);
    psprecomp::store32(rotate_state, 0x3804U, rotrv_r6_r10_r11);
    psprecomp::store32(rotate_state, 0x3808U, reserved_srl);
    rotate_state.gpr[10] = 0x12345678U;
    rotate_state.gpr[11] = 12U;
    CHECK(psprecomp::interpret_allegrex(rotate_state, 0x3800U));
    CHECK(rotate_state.gpr[5] == 0x78123456U);
    CHECK(psprecomp::interpret_allegrex(rotate_state, 0x3804U));
    CHECK(rotate_state.gpr[6] == 0x67812345U);
    CHECK(psprecomp::interpret_allegrex(rotate_state, 0x3808U));
    CHECK(rotate_state.stop_reason ==
          psprecomp::StopReason::unsupported_instruction);

    std::array<std::uint8_t, 8> allegrex_break_code{};
    psprecomp::State break_state;
    break_state.memory = allegrex_break_code.data();
    break_state.memory_size = allegrex_break_code.size();
    break_state.memory_base = 0x4000U;
    break_state.pc = break_state.memory_base;
    psprecomp::store32(break_state, 0x4000U, 0x0000000dU);
    psprecomp::store32(break_state, 0x4004U, 0x2402002aU);
    CHECK(psprecomp::interpret_allegrex(break_state, 0x4000U));
    CHECK(break_state.stop_reason == psprecomp::StopReason::running);
    CHECK(break_state.pc == 0x4004U);
    CHECK(psprecomp::interpret_allegrex(break_state, 0x4004U));
    CHECK(break_state.gpr[2] == 42U);

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

    psprecomp::store32(state, 0x1008, 0x44332211U);
    CHECK(psprecomp::load_word_left(state, 0x1009, 0xaabbccddU) ==
          0x2211ccddU);
    CHECK(psprecomp::load_word_right(state, 0x100a, 0xaabbccddU) ==
          0xaabb4433U);

    state.vfpu[psprecomp::vfpu_index(0, 1)] =
        std::bit_cast<std::uint32_t>(2.0F);
    state.vfpu[psprecomp::vfpu_index(1, 1)] =
        std::bit_cast<std::uint32_t>(3.0F);
    CHECK(psprecomp::vfpu_float(state, psprecomp::vfpu_index(0, 1)) == 2.0F);

    constexpr std::uint32_t vidt_q_r003 = 0xd00380a3U;
    CHECK(psprecomp::vfpu_opcode_supported(vidt_q_r003));
    CHECK(psprecomp::decode_allegrex(vidt_q_r003).lowering ==
          psprecomp::InstructionLowering::runtime_fallback);
    state.vfpu_ctrl[0] = 0xe4U;
    state.vfpu_ctrl[2] = 0U;
    psprecomp::execute_vfpu(state, vidt_q_r003, 0x1000U);
    float identity_row[4]{};
    psprecomp::vfpu_read_vector(state, vidt_q_r003 & 0x7fU, 4,
                               identity_row);
    CHECK(identity_row[0] == 0.0F && identity_row[1] == 0.0F &&
          identity_row[2] == 0.0F && identity_row[3] == 1.0F);

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

    std::array<std::uint8_t, 32> compact_relocated{};
    psprecomp::detail::write_u32(compact_relocated.data(), 0, 0x00000020U);
    psprecomp::detail::write_u32(compact_relocated.data(), 4, 0x08000010U);
    psprecomp::detail::write_u32(compact_relocated.data(), 8, 0x3c080001U);
    psprecomp::detail::write_u32(compact_relocated.data(), 12, 0x25088000U);
    const std::uint8_t compact_relocations[] = {1, 0x22, 0x23, 0x24};
    CHECK(psprecomp::apply_compact_psp_relocations(
              compact_relocated.data(), compact_relocated.size(),
              0x08800000U, segments, 1, compact_relocations,
              sizeof(compact_relocations), 4) ==
          psprecomp::RelocationResult::success);
    CHECK(compact_relocated == relocated);

    std::array<std::uint8_t, 40> repeated_relocated{};
    const psprecomp::PspLoadSegment repeated_segments[] = {
        {0, repeated_relocated.size(), 0}};
    psprecomp::detail::write_u32(repeated_relocated.data(), 0, 1U);
    psprecomp::detail::write_u32(repeated_relocated.data(), 32, 2U);
    const std::uint8_t repeated_metadata[] = {1, 0x45};
    CHECK(psprecomp::apply_compact_psp_relocations(
              repeated_relocated.data(), repeated_relocated.size(),
              0x08800000U, repeated_segments, 1, repeated_metadata,
              sizeof(repeated_metadata), 2) ==
          psprecomp::RelocationResult::success);
    CHECK(psprecomp::detail::read_u32(repeated_relocated.data(), 0) ==
          0x08800001U);
    CHECK(psprecomp::detail::read_u32(repeated_relocated.data(), 32) ==
          0x08800002U);
}
