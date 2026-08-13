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

    std::cout << "decoder_ns_per_instruction="
              << static_cast<double>(decode_ns) / iterations << '\n'
              << "interpreter_ns_per_instruction="
              << static_cast<double>(interpret_ns) / iterations << '\n'
              << "checksum=" << checksum << '\n';
}
