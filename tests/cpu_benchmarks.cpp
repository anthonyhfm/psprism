#include <psprecomp/allegrex_decoder.hpp>
#include <psprecomp/interpreter.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>

namespace {

using Clock = std::chrono::steady_clock;

template <typename Function>
std::uint64_t measure(Function&& function) {
    const auto start = Clock::now();
    function();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start)
            .count());
}

} // namespace

int main() {
    constexpr std::array corpus{
        0x2402002aU, 0x00432021U, 0x8c440000U, 0xac440004U,
        0x10400002U, 0x002a2a02U, 0x016a3046U, 0xd00380a3U,
    };
    constexpr std::uint64_t iterations = 4'000'000U;
    std::uint64_t checksum = 0;
    const auto decode_ns = measure([&] {
        for (std::uint64_t i = 0; i < iterations; ++i) {
            const auto decoded =
                psprecomp::decode_allegrex(corpus[i % corpus.size()]);
            checksum += decoded.op + static_cast<std::uint8_t>(decoded.lowering);
        }
    });

    std::array<std::uint8_t, 4> code{0x01U, 0x00U, 0x42U, 0x24U}; // addiu v0,v0,1
    psprecomp::State state;
    state.memory = code.data();
    state.memory_size = code.size();
    state.memory_base = 0x1000U;
    const auto interpret_ns = measure([&] {
        for (std::uint64_t i = 0; i < iterations; ++i) {
            state.pc = state.memory_base;
            state.stop_reason = psprecomp::StopReason::running;
            psprecomp::interpret_allegrex(state, state.pc);
        }
    });
    checksum += state.gpr[2];

    std::array<std::uint8_t, 4096> data{};
    psprecomp::State memory_state;
    memory_state.memory = data.data();
    memory_state.memory_size = data.size();
    memory_state.memory_base = 0x08800000U;
    const auto memory_ns = measure([&] {
        for (std::uint64_t i = 0; i < iterations; ++i) {
            const auto address = memory_state.memory_base +
                                 static_cast<std::uint32_t>((i & 1023U) * 4U);
            psprecomp::store32(memory_state, address,
                              static_cast<std::uint32_t>(i));
            checksum += psprecomp::load32(memory_state, address);
        }
    });

    psprecomp::State vfpu_state;
    for (std::uint32_t index = 0; index < 128U; ++index) {
        vfpu_state.vfpu[index] = std::bit_cast<std::uint32_t>(
            static_cast<float>(index + 1U) * 0.125F);
    }
    constexpr std::uint32_t vadd_q = 0x60008080U | 4U | (36U << 8U) |
                                     (68U << 16U);
    const auto varying_source = psprecomp::vfpu_index(36U, 4);
    const auto vfpu_static_ns = measure([&] {
        for (std::uint64_t i = 0; i < iterations; ++i) {
            vfpu_state.vfpu[varying_source] = std::bit_cast<std::uint32_t>(
                static_cast<float>((i & 255U) + 1U) * 0.125F);
            psprecomp::execute_vfpu_prefix_free<
                psprecomp::VfpuStaticOperation::add, 4>(vfpu_state, 4U, 36U,
                                                        68U);
            checksum += vfpu_state.vfpu[psprecomp::vfpu_index(4U, 4)];
        }
    });
    const auto vfpu_helper_ns = measure([&] {
        for (std::uint64_t i = 0; i < iterations; ++i) {
            vfpu_state.vfpu[varying_source] = std::bit_cast<std::uint32_t>(
                static_cast<float>((i & 255U) + 1U) * 0.125F);
            psprecomp::execute_vfpu(vfpu_state, vadd_q, 0x1000U);
            checksum += vfpu_state.vfpu[psprecomp::vfpu_index(4U, 4)];
        }
    });

    std::cout << "decoder_ns_per_instruction="
              << static_cast<double>(decode_ns) / iterations << '\n'
              << "interpreter_ns_per_instruction="
              << static_cast<double>(interpret_ns) / iterations << '\n'
              << "memory_roundtrip_ns="
              << static_cast<double>(memory_ns) / iterations << '\n'
              << "vfpu_static_ns="
              << static_cast<double>(vfpu_static_ns) / iterations << '\n'
              << "vfpu_helper_ns="
              << static_cast<double>(vfpu_helper_ns) / iterations << '\n'
              << "checksum=" << checksum << '\n';
}
