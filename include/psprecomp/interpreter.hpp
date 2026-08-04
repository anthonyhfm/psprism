#pragma once

#include <psprecomp/runtime.hpp>
#include <psprecomp/vfpu.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace psprecomp {

inline bool interpret_allegrex(State& state, std::uint32_t current_pc) {
    if ((current_pc & 3U) != 0U ||
        (!state.direct_memory_access && !address_ok(state, current_pc, 4))) {
        return false;
    }

    const auto instruction = PSPRECOMP_LOAD32(state, current_pc);
    if (state.stop_reason != StopReason::running) {
        return true;
    }
    const auto op = instruction >> 26U;
    const auto rs = (instruction >> 21U) & 31U;
    const auto rt = (instruction >> 16U) & 31U;
    const auto rd = (instruction >> 11U) & 31U;
    const auto shift = (instruction >> 6U) & 31U;
    const auto function = instruction & 63U;
    const auto immediate = instruction & 0xffffU;
    const auto signed_immediate = static_cast<std::int32_t>(
        static_cast<std::int16_t>(immediate));
    const auto address = state.gpr[rs] +
                         static_cast<std::uint32_t>(signed_immediate);
    const bool apply_delayed_branch = state.branch_pending;
    const auto delayed_target = state.branch_target;
    state.branch_pending = false;
    state.pc = current_pc + 4U;

    const auto branch = [&](bool condition, bool likely = false) {
        if (condition) {
            state.branch_pending = true;
            state.branch_target = current_pc + 4U +
                                  static_cast<std::uint32_t>(
                                      signed_immediate * 4);
        } else if (likely) {
            state.pc = current_pc + 8U;
        }
    };
    const auto fail = [&] {
        state.stop_reason = StopReason::unsupported_instruction;
        state.fault_pc = current_pc;
        state.fault_address = current_pc;
        state.fault_instruction = instruction;
    };

    switch (op) {
    case 0x00:
        switch (function) {
        case 0x00: state.gpr[rd] = state.gpr[rt] << shift; break;
        case 0x02: state.gpr[rd] = state.gpr[rt] >> shift; break;
        case 0x03:
            state.gpr[rd] = arithmetic_shift_right(state.gpr[rt], shift);
            break;
        case 0x04: state.gpr[rd] = state.gpr[rt] << (state.gpr[rs] & 31U); break;
        case 0x06: state.gpr[rd] = state.gpr[rt] >> (state.gpr[rs] & 31U); break;
        case 0x07:
            state.gpr[rd] = arithmetic_shift_right(state.gpr[rt],
                                                    state.gpr[rs]);
            break;
        case 0x08:
            state.branch_pending = true;
            state.branch_target = state.gpr[rs];
            break;
        case 0x09: {
            const auto target = state.gpr[rs];
            if (rd != 0) state.gpr[rd] = current_pc + 8U;
            state.branch_pending = true;
            state.branch_target = target;
            break;
        }
        case 0x0a:
            if (state.gpr[rt] == 0U) state.gpr[rd] = state.gpr[rs];
            break;
        case 0x0b:
            if (state.gpr[rt] != 0U) state.gpr[rd] = state.gpr[rs];
            break;
        case 0x0c: state.stop_reason = StopReason::syscall; break;
        case 0x0d: state.stop_reason = StopReason::breakpoint; break;
        case 0x0f: break;
        case 0x10: state.gpr[rd] = state.hi; break;
        case 0x11: state.hi = state.gpr[rs]; break;
        case 0x12: state.gpr[rd] = state.lo; break;
        case 0x13: state.lo = state.gpr[rs]; break;
        case 0x18: {
            const auto value = static_cast<std::int64_t>(as_s32(state.gpr[rs])) *
                               static_cast<std::int64_t>(as_s32(state.gpr[rt]));
            state.lo = static_cast<std::uint32_t>(value);
            state.hi = static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(value) >> 32U);
            break;
        }
        case 0x19: {
            const auto value = static_cast<std::uint64_t>(state.gpr[rs]) *
                               state.gpr[rt];
            state.lo = static_cast<std::uint32_t>(value);
            state.hi = static_cast<std::uint32_t>(value >> 32U);
            break;
        }
        case 0x1a: {
            const auto left = as_s32(state.gpr[rs]);
            const auto right = as_s32(state.gpr[rt]);
            if (right == 0) {
                state.lo = left < 0 ? 1U : 0xffffffffU;
                state.hi = state.gpr[rs];
            } else if (left == std::numeric_limits<std::int32_t>::min() &&
                       right == -1) {
                state.lo = static_cast<std::uint32_t>(left);
                state.hi = 0;
            } else {
                state.lo = static_cast<std::uint32_t>(left / right);
                state.hi = static_cast<std::uint32_t>(left % right);
            }
            break;
        }
        case 0x1b:
            if (state.gpr[rt] == 0U) {
                state.lo = 0xffffffffU;
                state.hi = state.gpr[rs];
            } else {
                state.lo = state.gpr[rs] / state.gpr[rt];
                state.hi = state.gpr[rs] % state.gpr[rt];
            }
            break;
        case 0x20:
        case 0x21: state.gpr[rd] = state.gpr[rs] + state.gpr[rt]; break;
        case 0x22:
        case 0x23: state.gpr[rd] = state.gpr[rs] - state.gpr[rt]; break;
        case 0x24: state.gpr[rd] = state.gpr[rs] & state.gpr[rt]; break;
        case 0x25: state.gpr[rd] = state.gpr[rs] | state.gpr[rt]; break;
        case 0x26: state.gpr[rd] = state.gpr[rs] ^ state.gpr[rt]; break;
        case 0x27: state.gpr[rd] = ~(state.gpr[rs] | state.gpr[rt]); break;
        case 0x2a:
            state.gpr[rd] = as_s32(state.gpr[rs]) < as_s32(state.gpr[rt]);
            break;
        case 0x2b: state.gpr[rd] = state.gpr[rs] < state.gpr[rt]; break;
        default: fail(); break;
        }
        break;
    case 0x01: {
        const bool ge = rt == 0x01 || rt == 0x03 || rt == 0x11 || rt == 0x13;
        const bool likely = rt == 0x02 || rt == 0x03 || rt == 0x12 || rt == 0x13;
        const bool link = rt >= 0x10 && rt <= 0x13;
        if (rt != 0x00 && rt != 0x01 && rt != 0x02 && rt != 0x03 &&
            rt != 0x10 && rt != 0x11 && rt != 0x12 && rt != 0x13) {
            fail();
            break;
        }
        if (link) state.gpr[31] = current_pc + 8U;
        branch(ge ? as_s32(state.gpr[rs]) >= 0 : as_s32(state.gpr[rs]) < 0,
               likely);
        break;
    }
    case 0x02:
    case 0x03:
        if (op == 0x03) state.gpr[31] = current_pc + 8U;
        state.branch_pending = true;
        state.branch_target = ((current_pc + 4U) & 0xf0000000U) |
                              ((instruction & 0x03ffffffU) << 2U);
        break;
    case 0x04: branch(state.gpr[rs] == state.gpr[rt]); break;
    case 0x05: branch(state.gpr[rs] != state.gpr[rt]); break;
    case 0x06: branch(as_s32(state.gpr[rs]) <= 0); break;
    case 0x07: branch(as_s32(state.gpr[rs]) > 0); break;
    case 0x08:
    case 0x09: state.gpr[rt] = state.gpr[rs] + signed_immediate; break;
    case 0x0a: state.gpr[rt] = as_s32(state.gpr[rs]) < signed_immediate; break;
    case 0x0b:
        state.gpr[rt] = state.gpr[rs] < static_cast<std::uint32_t>(signed_immediate);
        break;
    case 0x0c: state.gpr[rt] = state.gpr[rs] & immediate; break;
    case 0x0d: state.gpr[rt] = state.gpr[rs] | immediate; break;
    case 0x0e: state.gpr[rt] = state.gpr[rs] ^ immediate; break;
    case 0x0f: state.gpr[rt] = immediate << 16U; break;
    case 0x11: {
        const auto fmt = rs;
        const auto fs = rd;
        const auto fd = shift;
        if (fmt == 0x00) state.gpr[rt] = state.fpr[fs];
        else if (fmt == 0x04) state.fpr[fs] = state.gpr[rt];
        else if (fmt == 0x02) state.gpr[rt] = state.fcr31;
        else if (fmt == 0x06) state.fcr31 = state.gpr[rt];
        else if (fmt == 0x08) {
            const auto cc = (instruction >> 18U) & 7U;
            const auto bit = cc == 0 ? 23U : 24U + cc;
            const bool condition = ((state.fcr31 >> bit) & 1U) ==
                                   ((instruction >> 16U) & 1U);
            branch(condition, ((instruction >> 17U) & 1U) != 0U);
        } else if (fmt == 0x10) {
            const auto left = f32(state, fs);
            const auto right = f32(state, rt);
            switch (function) {
            case 0x00: set_f32(state, fd, left + right); break;
            case 0x01: set_f32(state, fd, left - right); break;
            case 0x02: set_f32(state, fd, left * right); break;
            case 0x03: set_f32(state, fd, left / right); break;
            case 0x04: set_f32(state, fd, std::sqrt(left)); break;
            case 0x05: set_f32(state, fd, std::fabs(left)); break;
            case 0x06: set_f32(state, fd, left); break;
            case 0x07: set_f32(state, fd, -left); break;
            case 0x0c:
                state.fpr[fd] = static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(std::round(left)));
                break;
            case 0x0d:
                state.fpr[fd] = static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(std::trunc(left)));
                break;
            default:
                if (function >= 0x30)
                    compare_f32(state, (instruction >> 8U) & 7U, left,
                                right, function & 15U);
                else fail();
                break;
            }
        } else if (fmt == 0x14 && function == 0x20) {
            set_f32(state, fd, static_cast<float>(as_s32(state.fpr[fs])));
        } else {
            fail();
        }
        break;
    }
    case 0x12:
    case 0x18:
    case 0x19:
    case 0x1b:
    case 0x32:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x3a:
    case 0x3c:
    case 0x3d:
    case 0x3e:
    case 0x3f:
        if (vfpu_opcode_supported(instruction))
            execute_vfpu(state, instruction, current_pc);
        else
            fail();
        break;
    case 0x14: branch(state.gpr[rs] == state.gpr[rt], true); break;
    case 0x15: branch(state.gpr[rs] != state.gpr[rt], true); break;
    case 0x16: branch(as_s32(state.gpr[rs]) <= 0, true); break;
    case 0x17: branch(as_s32(state.gpr[rs]) > 0, true); break;
    case 0x1c:
        if (function == 0x20) state.gpr[rd] = std::countl_zero(state.gpr[rs]);
        else if (function == 0x2c)
            state.gpr[rd] = as_s32(state.gpr[rs]) > as_s32(state.gpr[rt])
                                ? state.gpr[rs] : state.gpr[rt];
        else if (function == 0x2d)
            state.gpr[rd] = as_s32(state.gpr[rs]) < as_s32(state.gpr[rt])
                                ? state.gpr[rs] : state.gpr[rt];
        else fail();
        break;
    case 0x1f:
        if (function == 0x00) {
            const auto width = rd + 1U;
            const auto mask = width == 32U ? 0xffffffffU : (1U << width) - 1U;
            state.gpr[rt] = (state.gpr[rs] >> shift) & mask;
        } else if (function == 0x04) {
            const auto width = rd - shift + 1U;
            const auto mask = width == 32U ? 0xffffffffU : (1U << width) - 1U;
            const auto positioned = mask << shift;
            state.gpr[rt] = (state.gpr[rt] & ~positioned) |
                            ((state.gpr[rs] << shift) & positioned);
        } else if (function == 0x20 && shift == 0x10) {
            state.gpr[rd] = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(static_cast<std::int8_t>(state.gpr[rt])));
        } else if (function == 0x20 && shift == 0x18) {
            state.gpr[rd] = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(static_cast<std::int16_t>(state.gpr[rt])));
        } else {
            fail();
        }
        break;
    case 0x20:
        state.gpr[rt] = static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::int8_t>(PSPRECOMP_LOAD8(state, address))));
        break;
    case 0x21:
        state.gpr[rt] = static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::int16_t>(PSPRECOMP_LOAD16(state, address))));
        break;
    case 0x22: state.gpr[rt] = load_word_left(state, address, state.gpr[rt]); break;
    case 0x23: state.gpr[rt] = PSPRECOMP_LOAD32(state, address); break;
    case 0x24: state.gpr[rt] = PSPRECOMP_LOAD8(state, address); break;
    case 0x25: state.gpr[rt] = PSPRECOMP_LOAD16(state, address); break;
    case 0x26: state.gpr[rt] = load_word_right(state, address, state.gpr[rt]); break;
    case 0x28: PSPRECOMP_STORE8(state, address, state.gpr[rt]); break;
    case 0x29: PSPRECOMP_STORE16(state, address, state.gpr[rt]); break;
    case 0x2a: store_word_left(state, address, state.gpr[rt]); break;
    case 0x2b: PSPRECOMP_STORE32(state, address, state.gpr[rt]); break;
    case 0x2e: store_word_right(state, address, state.gpr[rt]); break;
    case 0x2f:
    case 0x33: break;
    case 0x30: state.gpr[rt] = PSPRECOMP_LOAD32(state, address); break;
    case 0x31: state.fpr[rt] = PSPRECOMP_LOAD32(state, address); break;
    case 0x38:
        PSPRECOMP_STORE32(state, address, state.gpr[rt]);
        state.gpr[rt] = 1U;
        break;
    case 0x39: PSPRECOMP_STORE32(state, address, state.fpr[rt]); break;
    default: fail(); break;
    }

    state.gpr[0] = 0;
    if (state.stop_reason == StopReason::running && apply_delayed_branch) {
        state.pc = delayed_target;
    }
    return true;
}

} // namespace psprecomp
