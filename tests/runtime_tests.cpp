#include <psprecomp/overlay.hpp>
#include <psprecomp/allegrex_decoder.hpp>
#include <psprecomp/interpreter.hpp>
#include <psprecomp/patch.hpp>
#include <psprecomp/relocation.hpp>
#include <psprecomp/runtime.hpp>
#include <psprecomp/vfpu.hpp>

#include <array>
#include <atomic>
#include <functional>
#include <thread>

namespace psprecomp::patch::bridge {
bool execute_function_at(State& state, std::uint32_t address,
                         std::uint64_t max_blocks) {
    static_cast<void>(max_blocks);
    if (const auto* patch = find_patch(state, address)) {
        return invoke_patch(*patch, state, address);
    }
    if (address == 0x08803000U) {
        state.gpr[2] = state.gpr[4] * state.gpr[5];
        state.pc = state.gpr[31];
        return true;
    }
    if (address == 0x08803020U) {
        state.gpr[2] = 0;
        for (std::size_t index = 0; index < 8; ++index) {
            state.gpr[2] += state.gpr[4 + index];
        }
        state.gpr[2] += load32(state, state.gpr[29]);
        state.pc = state.gpr[31];
        return true;
    }
    if (address == 0x08803040U) {
        state.gpr[2] = state.gpr[4] + state.gpr[5];
        state.pc = state.gpr[31];
        return true;
    }
    if (address == 0x08803060U) {
        const auto value = load32(state, state.gpr[4]) + state.gpr[5];
        store32(state, state.gpr[4], value);
        state.gpr[2] = state.gpr[4];
        state.pc = state.gpr[31];
        return true;
    }
    if (address == 0x08803080U) {
        state.gpr[2] = 0x09900000U;
        state.pc = state.gpr[31];
        return true;
    }
    return false;
}
} // namespace psprecomp::patch::bridge

namespace {

int custom_test_add(int a, int b) {
    return a + b + 10;
}
float custom_test_calc(float a, float b) {
    return a * b + 1.5f;
}
std::uint64_t custom_test_u64(std::uint64_t a, std::uint32_t b) {
    return a + b;
}
int custom_test_nine_ints(int a, int b, int c, int d, int e, int f, int g,
                          int h, int i) {
    return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8 + i * 9;
}
float custom_test_nine_floats(float a, float b, float c, float d, float e,
                              float f, float g, float h, float i) {
    return a + b + c + d + e + f + g + h + i;
}
double custom_test_double(double value, int addend) {
    return value + static_cast<double>(addend);
}
int* custom_test_pointer(int* value, int delta) {
    *value += delta;
    return value;
}
float custom_test_mixed(int a, float af, int b, float bf, int c, float cf,
                        int d, float df, int e, float ef, int f, float ff,
                        int g, float gf, int h, float hf, int i, float inf) {
    return static_cast<float>(a + b + c + d + e + f + g + h + i) +
           af + bf + cf + df + ef + ff + gf + hf + inf;
}
std::uint64_t custom_test_wide_spill(
    int a, int b, int c, int d, int e, int f, int g, int h, int i,
    std::uint64_t wide, int j) {
    return wide + static_cast<std::uint64_t>(
        a + b + c + d + e + f + g + h + i + j);
}
int custom_test_original(int a, int b) {
    return psprecomp::patch::call_original<int>(a, b) + 10;
}
int ghidra_call_total{};
void FUN_00053ba8(int param_1, int param_2) {
    ghidra_call_total = param_1 + param_2;
}
bool dax_initialized{};
void DaxRenderWorld_Initialize() {
    dax_initialized = true;
}
int initializer_calls{};
void patch_test_initializer() {
    ++initializer_calls;
}

RECOMP_PATCH_FUNCTION(0x08801000U, custom_test_add);
RECOMP_PATCH_FUNCTION(0x08801020U, custom_test_calc);
RECOMP_PATCH_FUNCTION(0x08801040U, custom_test_u64);
RECOMP_PATCH_FUNCTION(0x08801060U, custom_test_nine_ints);
RECOMP_PATCH_FUNCTION(0x08801080U, custom_test_nine_floats);
RECOMP_PATCH_FUNCTION(0x088010a0U, custom_test_double);
RECOMP_PATCH_FUNCTION(0x088010c0U, custom_test_pointer);
RECOMP_PATCH_FUNCTION(0x088010e0U, custom_test_mixed);
RECOMP_PATCH_FUNCTION(0x08801100U, custom_test_wide_spill);
RECOMP_PATCH_FUNCTION(0x08803040U, custom_test_original);
RECOMP_PATCH_GHIDRA(FUN_00053ba8);
RECOMP_PATCH_FUNCTION_BY_NAME("DaxRenderWorld_Initialize",
                              DaxRenderWorld_Initialize);
RECOMP_PATCH_INITIALIZER(patch_test_initializer);

constexpr std::uint32_t vector_word(std::uint32_t base, int size,
                                    std::uint32_t vd, std::uint32_t vs,
                                    std::uint32_t vt) {
    const auto size_bits = size == 1   ? 0U
                           : size == 2 ? 1U << 7U
                           : size == 3 ? 1U << 15U
                                       : (1U << 15U) | (1U << 7U);
    return base | size_bits | vd | (vs << 8U) | (vt << 16U);
}

template <psprecomp::VfpuStaticOperation Operation, int Size>
bool vfpu_static_matches_helper(std::uint32_t instruction) {
    psprecomp::State expected;
    for (std::uint32_t index = 0; index < 128U; ++index) {
        const auto value = (static_cast<int>(index % 23U) - 11) * 0.25F;
        expected.vfpu[index] = std::bit_cast<std::uint32_t>(value);
    }
    auto actual = expected;
    psprecomp::execute_vfpu(expected, instruction, 0x1000U);
    psprecomp::execute_vfpu_prefix_free<Operation, Size>(
        actual, instruction & 0x7fU, (instruction >> 8U) & 0x7fU,
        (instruction >> 16U) & 0x7fU);
    return std::equal(std::begin(expected.vfpu), std::end(expected.vfpu),
                      std::begin(actual.vfpu)) &&
           std::equal(std::begin(expected.vfpu_ctrl),
                      std::end(expected.vfpu_ctrl),
                      std::begin(actual.vfpu_ctrl));
}

template <psprecomp::VfpuStaticOperation Operation>
bool vfpu_static_matches_all_sizes(std::uint32_t base,
                                   std::uint32_t vt = 9U) {
    return vfpu_static_matches_helper<Operation, 1>(
               vector_word(base, 1, 3U, 5U, vt)) &&
           vfpu_static_matches_helper<Operation, 2>(
               vector_word(base, 2, 35U, 12U, vt)) &&
           vfpu_static_matches_helper<Operation, 3>(
               vector_word(base, 3, 66U, 33U, vt)) &&
           vfpu_static_matches_helper<Operation, 4>(
               vector_word(base, 4, 69U, 37U, vt));
}

} // namespace

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
    CHECK(state.cpu_profile_enabled == psprecomp::cpu_profiling_compiled);
    state.cpu_profile_enabled = true;

    psprecomp::note_cpu_import_dispatch(state);
    CHECK(state.cpu_profile.import_dispatches == 1U);

    psprecomp::store32(state, 0x1004, 0x89abcdefU);
    CHECK(state.stop_reason == psprecomp::StopReason::running);
    CHECK(psprecomp::load32(state, 0x1004) == 0x89abcdefU);
    CHECK(psprecomp::load32(state, 0x40001004U) == 0x89abcdefU);
    psprecomp::store32(state, 0x80001008U, 0x12345678U);
    CHECK(psprecomp::load32(state, 0xa0001008U) == 0x12345678U);
    CHECK(memory[4] == 0xef && memory[7] == 0x89);
    CHECK(state.cpu_profile.memory_writes == 2U);
    CHECK(state.cpu_profile.memory_reads == 3U);

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
    CHECK(state.cpu_profile.memory_faults == 1U);
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
    CHECK(psprecomp::load_word_left(state, 0x1008, 0xaabbccddU) == 0x11bbccddU);
    CHECK(psprecomp::load_word_left(state, 0x1009, 0xaabbccddU) == 0x2211ccddU);
    CHECK(psprecomp::load_word_left(state, 0x100a, 0xaabbccddU) == 0x332211ddU);
    CHECK(psprecomp::load_word_left(state, 0x100b, 0xaabbccddU) == 0x44332211U);

    CHECK(psprecomp::load_word_right(state, 0x1008, 0xaabbccddU) == 0x44332211U);
    CHECK(psprecomp::load_word_right(state, 0x1009, 0xaabbccddU) == 0xaa443322U);
    CHECK(psprecomp::load_word_right(state, 0x100a, 0xaabbccddU) == 0xaabb4433U);
    CHECK(psprecomp::load_word_right(state, 0x100b, 0xaabbccddU) == 0xaabbcc44U);

    psprecomp::store32(state, 0x1000, 0x00000000U);
    psprecomp::store_word_left(state, 0x1000, 0x88776655U);
    CHECK(memory[0] == 0x88);
    psprecomp::store32(state, 0x1000, 0x00000000U);
    psprecomp::store_word_left(state, 0x1001, 0x88776655U);
    CHECK(memory[0] == 0x77 && memory[1] == 0x88);
    psprecomp::store32(state, 0x1000, 0x00000000U);
    psprecomp::store_word_left(state, 0x1002, 0x88776655U);
    CHECK(memory[0] == 0x66 && memory[1] == 0x77 && memory[2] == 0x88);
    psprecomp::store32(state, 0x1000, 0x00000000U);
    psprecomp::store_word_left(state, 0x1003, 0x88776655U);
    CHECK(memory[0] == 0x55 && memory[1] == 0x66 && memory[2] == 0x77 && memory[3] == 0x88);

    psprecomp::store32(state, 0x1000, 0x00000000U);
    psprecomp::store_word_right(state, 0x1000, 0x88776655U);
    CHECK(memory[0] == 0x55 && memory[1] == 0x66 && memory[2] == 0x77 && memory[3] == 0x88);
    psprecomp::store32(state, 0x1000, 0x00000000U);
    psprecomp::store_word_right(state, 0x1001, 0x88776655U);
    CHECK(memory[1] == 0x55 && memory[2] == 0x66 && memory[3] == 0x77);
    psprecomp::store32(state, 0x1000, 0x00000000U);
    psprecomp::store_word_right(state, 0x1002, 0x88776655U);
    CHECK(memory[2] == 0x55 && memory[3] == 0x66);
    psprecomp::store32(state, 0x1000, 0x00000000U);
    psprecomp::store_word_right(state, 0x1003, 0x88776655U);
    CHECK(memory[3] == 0x55);

    state.vfpu[psprecomp::vfpu_index(0, 1)] =
        std::bit_cast<std::uint32_t>(2.0F);
    state.vfpu[psprecomp::vfpu_index(1, 1)] =
        std::bit_cast<std::uint32_t>(3.0F);
    CHECK(psprecomp::vfpu_float(state, psprecomp::vfpu_index(0, 1)) == 2.0F);

    constexpr std::uint32_t vidt_q_r003 = 0xd00380a3U;
    CHECK(psprecomp::vfpu_opcode_supported(vidt_q_r003));
    CHECK(psprecomp::decode_allegrex(vidt_q_r003).lowering ==
          psprecomp::InstructionLowering::runtime_fallback);

    constexpr auto vadd_s = vector_word(0x60000000U, 1, 3U, 5U, 9U);
    constexpr auto vsub_p = vector_word(0x60800000U, 2, 35U, 12U, 7U);
    constexpr auto vmul_t = vector_word(0x64000000U, 3, 66U, 33U, 44U);
    constexpr auto vdot_q = vector_word(0x64800000U, 4, 4U, 36U, 68U);
    constexpr auto vmov_q = vector_word(0xd0000000U, 4, 69U, 37U, 0U);
    static_assert(psprecomp::vfpu_static_operation(vadd_s) ==
                  psprecomp::VfpuStaticOperation::add);
    static_assert(psprecomp::decode_allegrex(vadd_s).lowering ==
                  psprecomp::InstructionLowering::guarded_native);
    CHECK((vfpu_static_matches_helper<psprecomp::VfpuStaticOperation::add, 1>(
        vadd_s)));
    CHECK((vfpu_static_matches_helper<
           psprecomp::VfpuStaticOperation::subtract, 2>(vsub_p)));
    CHECK((vfpu_static_matches_helper<
           psprecomp::VfpuStaticOperation::multiply, 3>(vmul_t)));
    CHECK((vfpu_static_matches_helper<psprecomp::VfpuStaticOperation::dot, 4>(
        vdot_q)));
    CHECK((vfpu_static_matches_helper<psprecomp::VfpuStaticOperation::move, 4>(
        vmov_q)));
    CHECK((vfpu_static_matches_all_sizes<
           psprecomp::VfpuStaticOperation::add>(0x60000000U)));
    CHECK((vfpu_static_matches_all_sizes<
           psprecomp::VfpuStaticOperation::subtract>(0x60800000U)));
    CHECK((vfpu_static_matches_all_sizes<
           psprecomp::VfpuStaticOperation::multiply>(0x64000000U)));
    CHECK((vfpu_static_matches_all_sizes<
           psprecomp::VfpuStaticOperation::dot>(0x64800000U)));
    CHECK((vfpu_static_matches_all_sizes<
           psprecomp::VfpuStaticOperation::move>(0xd0000000U, 0U)));

    // This scalar VDIV encoding must remain a VFPU fallback rather than being
    // rejected as invalid code.
    constexpr std::uint32_t vdiv_s = 0x63c06000U;
    CHECK(psprecomp::vfpu_opcode_supported(vdiv_s));
    CHECK(psprecomp::decode_allegrex(vdiv_s).lowering ==
          psprecomp::InstructionLowering::runtime_fallback);
    state.vfpu[psprecomp::vfpu_index((vdiv_s >> 8U) & 0x7fU, 1)] =
        std::bit_cast<std::uint32_t>(6.0F);
    state.vfpu[psprecomp::vfpu_index((vdiv_s >> 16U) & 0x7fU, 1)] =
        std::bit_cast<std::uint32_t>(2.0F);
    psprecomp::execute_vfpu(state, vdiv_s, 0x1000U);
    CHECK(psprecomp::vfpu_float(
              state, psprecomp::vfpu_index(vdiv_s & 0x7fU, 1)) == 3.0F);

    constexpr std::uint32_t vf2in_s = 0xd2000000U;
    constexpr std::uint32_t vf2iz_s = 0xd2200000U;
    constexpr std::uint32_t vf2iu_s = 0xd2400000U;
    constexpr std::uint32_t vf2id_s = 0xd2600000U;
    const auto vfpu_conversion_result = [&](std::uint32_t instruction,
                                            float input) {
        state.vfpu[psprecomp::vfpu_index(0, 1)] =
            std::bit_cast<std::uint32_t>(input);
        psprecomp::execute_vfpu(state, instruction, 0x1000U);
        return std::bit_cast<std::int32_t>(
            state.vfpu[psprecomp::vfpu_index(0, 1)]);
    };
    CHECK(vfpu_conversion_result(vf2in_s, -1.6F) == -2);
    CHECK(vfpu_conversion_result(vf2iz_s, -1.6F) == -1);
    CHECK(vfpu_conversion_result(vf2iu_s, -1.6F) == -1);
    CHECK(vfpu_conversion_result(vf2id_s, -1.6F) == -2);
    CHECK(vfpu_conversion_result(vf2id_s, -0.25F) == -1);

    auto prefixed_state = state;
    prefixed_state.vfpu_ctrl[0] = 0x0000001bU;
    CHECK(!psprecomp::vfpu_prefixes_identity(prefixed_state));
    psprecomp::note_vfpu_static_lowering(state);
    psprecomp::note_vfpu_helper_fallback(state);
    CHECK(state.cpu_profile.vfpu_static_lowerings == 1U);
    CHECK(state.cpu_profile.vfpu_helper_fallbacks == 1U);
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

    // Patch system verification
    {
        alignas(16) std::array<std::uint8_t, 1024> ram{};
        psprecomp::State state;
        state.memory = ram.data();
        state.memory_size = ram.size();
        state.memory_base = 0x08800000U;

        // 1. Integer function replacement
        const auto* patch_add = psprecomp::patch::find_patch(0x08801000U);
        CHECK(patch_add != nullptr);
        state.gpr[4] = 15;
        state.gpr[5] = 25;
        state.gpr[31] = 0x08809999U;
        CHECK(patch_add->hook(state));
        CHECK(state.gpr[2] == 50);
        CHECK(state.pc == 0x08809999U);

        // 2. Float function replacement
        const auto* patch_calc = psprecomp::patch::find_patch(0x08801020U);
        CHECK(patch_calc != nullptr);
        psprecomp::set_f32(state, 12, 3.0f);
        psprecomp::set_f32(state, 13, 4.0f);
        state.gpr[31] = 0x08809999U;
        CHECK(patch_calc->hook(state));
        CHECK(psprecomp::f32(state, 0) == 13.5f);
        CHECK(state.pc == 0x08809999U);

        // 3. 64-bit int replacement
        const auto* patch_u64 = psprecomp::patch::find_patch(0x08801040U);
        CHECK(patch_u64 != nullptr);
        state.gpr[4] = 0x00000005U;
        state.gpr[5] = 0x00000001U; // 0x100000005
        state.gpr[6] = 10;
        state.gpr[31] = 0x08809999U;
        CHECK(patch_u64->hook(state));
        CHECK(state.gpr[2] == 0x0000000fU);
        CHECK(state.gpr[3] == 0x00000001U);

        // 4. Memory helper functions
        psprecomp::patch::write_guest<std::uint32_t>(state, 0x08800100U, 0xdeadbeefU);
        CHECK(psprecomp::patch::read_guest<std::uint32_t>(state, 0x08800100U) == 0xdeadbeefU);
        psprecomp::patch::write_guest<float>(state, 0x08800104U, 123.456f);
        CHECK(psprecomp::patch::read_guest<float>(state, 0x08800104U) == 123.456f);

        // 5. Calling game functions with typed arguments
        psprecomp::patch::StateGuard guard(state);
        const auto mul_result = psprecomp::patch::call_game_function<int>(0x08803000U, 7, 8);
        CHECK(mul_result == 56);

        // 6. PSP EABI uses eight integer and eight float argument registers.
        state.gpr[29] = 0x08800300U;
        for (std::uint32_t index = 0; index < 8; ++index) {
            state.gpr[4 + index] = index + 1U;
        }
        psprecomp::store32(state, state.gpr[29], 9U);
        state.gpr[31] = 0x08809999U;
        const auto* patch_nine_ints =
            psprecomp::patch::find_patch(state, 0x08801060U);
        CHECK(patch_nine_ints != nullptr);
        CHECK(psprecomp::patch::invoke_patch(
            *patch_nine_ints, state, 0x08801060U));
        CHECK(state.gpr[2] == 285U);

        for (std::uint32_t index = 0; index < 8; ++index) {
            psprecomp::set_f32(state, 12U + index,
                               static_cast<float>(index + 1U));
        }
        psprecomp::patch::write_guest<float>(state, state.gpr[29], 9.0f);
        state.gpr[31] = 0x08809999U;
        const auto* patch_nine_floats =
            psprecomp::patch::find_patch(state, 0x08801080U);
        CHECK(patch_nine_floats != nullptr);
        CHECK(psprecomp::patch::invoke_patch(
            *patch_nine_floats, state, 0x08801080U));
        CHECK(psprecomp::f32(state, 0) == 45.0f);

        // 7. PSP doubles are passed and returned in aligned GPR pairs.
        const auto double_bits = psprecomp::patch::detail::to_wide_word(2.5);
        state.gpr[4] = static_cast<std::uint32_t>(double_bits);
        state.gpr[5] = static_cast<std::uint32_t>(double_bits >> 32U);
        state.gpr[6] = 4U;
        state.gpr[31] = 0x08809999U;
        const auto* patch_double =
            psprecomp::patch::find_patch(state, 0x088010a0U);
        CHECK(patch_double != nullptr);
        CHECK(psprecomp::patch::invoke_patch(
            *patch_double, state, 0x088010a0U));
        const auto returned_double =
            psprecomp::patch::detail::from_wide_word<double>(
                static_cast<std::uint64_t>(state.gpr[2]) |
                (static_cast<std::uint64_t>(state.gpr[3]) << 32U));
        CHECK(returned_double == 6.5);

        // Pointer parameters are translated between 32-bit guest addresses
        // and native pointers, including pointer return values.
        psprecomp::store32(state, 0x08800120U, 37U);
        state.gpr[4] = 0x08800120U;
        state.gpr[5] = 5U;
        state.gpr[31] = 0x08809999U;
        const auto* patch_pointer =
            psprecomp::patch::find_patch(state, 0x088010c0U);
        CHECK(patch_pointer != nullptr);
        CHECK(psprecomp::patch::invoke_patch(
            *patch_pointer, state, 0x088010c0U));
        CHECK(psprecomp::load32(state, 0x08800120U) == 42U);
        CHECK(state.gpr[2] == 0x08800120U);

        // The same pointer translation applies to outbound typed calls.
        {
            psprecomp::patch::StateGuard pointer_guard(state);
            auto* const host_value =
                reinterpret_cast<int*>(ram.data() + 0x120U);
            auto* const returned = psprecomp::patch::call_game_function<int*>(
                0x08803060U, host_value, 8);
            CHECK(returned == host_value);
            CHECK(psprecomp::load32(state, 0x08800120U) == 50U);

            int unmapped_host_value{};
            CHECK(psprecomp::patch::call_game_function<int>(
                      0x08803000U, &unmapped_host_value) == 0);
            CHECK(psprecomp::patch::last_call_error() ==
                  psprecomp::patch::CallError::unmapped_pointer);

            state.stop_reason = psprecomp::StopReason::running;
            CHECK(psprecomp::patch::call_game_function<int*>(
                      0x08803080U) == nullptr);
            CHECK(psprecomp::patch::last_call_error() ==
                  psprecomp::patch::CallError::unmapped_pointer);
            CHECK(state.stop_reason == psprecomp::StopReason::memory_fault);
            state.stop_reason = psprecomp::StopReason::running;
            state.fault_address = 0;
            state.fault_pc = 0;
        }

        // Invalid guest pointers stop before the replacement can dereference
        // them and are reported as guest memory faults.
        state.stop_reason = psprecomp::StopReason::running;
        state.gpr[4] = 0x09900000U;
        state.gpr[5] = 1U;
        CHECK(psprecomp::patch::invoke_patch(
            *patch_pointer, state, 0x088010c0U));
        CHECK(state.stop_reason == psprecomp::StopReason::memory_fault);
        CHECK(state.fault_address == 0x09900000U);
        state.stop_reason = psprecomp::StopReason::running;
        state.fault_address = 0;
        state.fault_pc = 0;

        // Integer and float register banks spill into one ordered stack area.
        for (std::uint32_t index = 0; index < 8; ++index) {
            state.gpr[4U + index] = index + 1U;
            psprecomp::set_f32(state, 12U + index,
                               static_cast<float>(index + 1U));
        }
        psprecomp::store32(state, state.gpr[29], 9U);
        psprecomp::patch::write_guest<float>(
            state, state.gpr[29] + 4U, 9.0F);
        state.gpr[31] = 0x08809999U;
        const auto* patch_mixed =
            psprecomp::patch::find_patch(state, 0x088010e0U);
        CHECK(patch_mixed != nullptr);
        CHECK(psprecomp::patch::invoke_patch(
            *patch_mixed, state, 0x088010e0U));
        CHECK(psprecomp::f32(state, 0) == 90.0F);

        // A spilled 64-bit argument starts on an aligned stack pair. The
        // compiler leaves stack word 1 unused after the ninth integer.
        for (std::uint32_t index = 0; index < 8; ++index)
            state.gpr[4U + index] = index + 1U;
        constexpr std::uint64_t spilled_wide = 0x1122334455667788ULL;
        psprecomp::store32(state, state.gpr[29], 9U);
        psprecomp::store32(state, state.gpr[29] + 4U, 0xdeadbeefU);
        psprecomp::store32(state, state.gpr[29] + 8U,
                          static_cast<std::uint32_t>(spilled_wide));
        psprecomp::store32(state, state.gpr[29] + 12U,
                          static_cast<std::uint32_t>(spilled_wide >> 32U));
        psprecomp::store32(state, state.gpr[29] + 16U, 10U);
        state.gpr[31] = 0x08809999U;
        const auto* patch_wide_spill =
            psprecomp::patch::find_patch(state, 0x08801100U);
        CHECK(patch_wide_spill != nullptr);
        CHECK(psprecomp::patch::invoke_patch(
            *patch_wide_spill, state, 0x08801100U));
        const auto wide_spill_result =
            static_cast<std::uint64_t>(state.gpr[2]) |
            (static_cast<std::uint64_t>(state.gpr[3]) << 32U);
        CHECK(wide_spill_result == spilled_wide + 55U);

        // 8. Ghidra FUN_ names resolve as offsets; descriptive names use the
        // generated code-map symbol passed by the dispatcher.
        const auto* ghidra_patch = psprecomp::patch::find_patch(
            state, state.memory_base + 0x00053ba8U);
        CHECK(ghidra_patch != nullptr);
        state.gpr[4] = 12U;
        state.gpr[5] = 30U;
        state.gpr[31] = 0x08809999U;
        CHECK(psprecomp::patch::invoke_patch(
            *ghidra_patch, state, state.memory_base + 0x00053ba8U));
        CHECK(ghidra_call_total == 42);

        const auto* named_patch = psprecomp::patch::find_patch(
            state, 0x08802000U, "DaxRenderWorld_Initialize");
        CHECK(named_patch != nullptr);
        state.gpr[31] = 0x08809999U;
        CHECK(psprecomp::patch::invoke_patch(
            *named_patch, state, 0x08802000U));
        CHECK(dax_initialized);

        // 9. Typed globals accept image offsets and can be overwritten by a
        // startup initializer or any active hook.
        psprecomp::patch::GameGlobal<std::uint32_t> test_global(
            psprecomp::patch::image_offset(0x100U));
        test_global = 0xcafebabeU;
        CHECK(test_global.get() == 0xcafebabeU);
        CHECK(test_global.pointer() ==
              reinterpret_cast<std::uint32_t*>(ram.data() + 0x100U));
        psprecomp::patch::GameGlobal<int*> test_pointer_global(
            psprecomp::patch::image_offset(0x130U));
        auto* const mapped_value =
            reinterpret_cast<int*>(ram.data() + 0x120U);
        test_pointer_global = mapped_value;
        CHECK(psprecomp::load32(state, 0x08800130U) == 0x08800120U);
        CHECK(test_pointer_global.get() == mapped_value);
        CHECK(psprecomp::patch::run_initializers(state));
        CHECK(initializer_calls == 1);

        // 10. Outbound calls use the same eight-register ABI and reserve a
        // temporary outgoing stack area for spilled arguments.
        state.gpr[29] = 0x088003c0U;
        const auto sum_nine = psprecomp::patch::call_game_function<int>(
            0x08803020U, 1, 2, 3, 4, 5, 6, 7, 8, 9);
        CHECK(sum_nine == 45);
        CHECK(state.gpr[29] == 0x088003c0U);
        CHECK(psprecomp::patch::last_call_error() ==
              psprecomp::patch::CallError::none);

        // 11. A replacement can call the translated original at the same
        // address without recursively entering itself.
        const auto hooked_original = psprecomp::patch::call_game_function<int>(
            0x08803040U, 7, 8);
        CHECK(hooked_original == 25);
    }

    // Active patch state is isolated between concurrently executing guest
    // threads.
    {
        psprecomp::State first;
        psprecomp::State second;
        std::atomic<int> ready{};
        std::atomic<bool> first_ok{};
        std::atomic<bool> second_ok{};
        auto check_thread_state = [&](psprecomp::State& state,
                                      std::atomic<bool>& result) {
            psprecomp::patch::StateGuard guard(state);
            ready.fetch_add(1, std::memory_order_release);
            while (ready.load(std::memory_order_acquire) != 2) {
                std::this_thread::yield();
            }
            result.store(psprecomp::patch::active_state() == &state,
                         std::memory_order_release);
        };
        std::thread first_thread(check_thread_state, std::ref(first),
                                 std::ref(first_ok));
        std::thread second_thread(check_thread_state, std::ref(second),
                                  std::ref(second_ok));
        first_thread.join();
        second_thread.join();
        CHECK(first_ok.load(std::memory_order_acquire));
        CHECK(second_ok.load(std::memory_order_acquire));
    }
}
