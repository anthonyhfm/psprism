#include "../emitter.hpp"
#include "internal.hpp"
#include <psprecomp/allegrex_decoder.hpp>
#include <psprecomp/vfpu.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace psprecomp::detail
{

    std::string reg(std::uint32_t index)
    {
        return "state.gpr[" + std::to_string(index) + "]";
    }

    std::string immediate(std::uint32_t instruction, bool relocated)
    {
        if (!relocated)
        {
            return hex(instruction & 0xffffU);
        }
        return "instruction_immediate(state, current_pc, " + hex(instruction) + ")";
    }

    std::string signed_immediate(std::uint32_t instruction, bool relocated)
    {
        if (!relocated)
        {
            return hex(static_cast<std::uint32_t>(static_cast<std::int32_t>(
                static_cast<std::int16_t>(instruction & 0xffffU))));
        }
        return "instruction_signed_immediate(state, current_pc, " +
               hex(instruction) + ")";
    }

    std::string signed_immediate_s32(std::uint32_t instruction, bool relocated)
    {
        if (!relocated)
        {
            return "static_cast<std::int32_t>(" +
                   signed_immediate(instruction, false) + ")";
        }
        return "instruction_signed_immediate_s32(state, current_pc, " +
               hex(instruction) + ")";
    }

    std::string signed_imm(std::uint32_t instruction, bool relocated)
    {
        return " + " + signed_immediate(instruction, relocated);
    }

    std::string branch_target(std::uint32_t pc, std::uint32_t instruction,
                              bool relocated)
    {
        if (!relocated)
        {
            const auto displacement = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(static_cast<std::int16_t>(instruction)) *
                4);
            return "state.memory_base + " + hex(pc + 4U + displacement);
        }
        return "instruction_branch_target(state, current_pc, " + hex(instruction) +
               ")";
    }

    std::string jump_target(std::uint32_t pc, std::uint32_t instruction,
                            bool relocated, std::uint32_t preferred_base)
    {
        if (!relocated)
        {
            const auto original_pc = pc + preferred_base;
            const auto target =
                (((original_pc + 4U) & 0xf0000000U) |
                 ((instruction & 0x03ffffffU) << 2U)) -
                preferred_base;
            return "state.memory_base + " + hex(target);
        }
        return "instruction_jump_target(state, current_pc, " + hex(instruction) +
               ")";
    }

    std::string opcode_group(std::uint32_t instruction)
    {
        const auto decoded = decode_allegrex(instruction);
        if (decoded.valid())
        {
            return std::string(decoded.name);
        }
        const auto op = instruction >> 26U;
        std::ostringstream stream;
        stream << "op_" << std::hex << std::setfill('0') << std::setw(2) << op;
        if (op == 0)
        {
            stream << "_special_" << std::setw(2) << (instruction & 63U);
        }
        else if (op == 1)
        {
            stream << "_regimm_" << std::setw(2) << ((instruction >> 16U) & 31U);
        }
        else if (op >= 0x10 && op <= 0x12)
        {
            stream << "_rs_" << std::setw(2) << ((instruction >> 21U) & 31U);
        }
        return stream.str();
    }

    std::string emit_guarded_vfpu(std::uint32_t pc, std::uint32_t instruction,
                                  VfpuStaticOperation operation)
    {
        const auto vd = instruction & 0x7fU;
        const auto vs = (instruction >> 8U) & 0x7fU;
        const auto vt = (instruction >> 16U) & 0x7fU;
        const auto size = vfpu_size(instruction);
        std::string_view operation_name;
        switch (operation)
        {
        case VfpuStaticOperation::add:
            operation_name = "add";
            break;
        case VfpuStaticOperation::subtract:
            operation_name = "subtract";
            break;
        case VfpuStaticOperation::multiply:
            operation_name = "multiply";
            break;
        case VfpuStaticOperation::dot:
            operation_name = "dot";
            break;
        case VfpuStaticOperation::move:
            operation_name = "move";
            break;
        default:
            throw std::logic_error("invalid static VFPU operation");
        }
        std::ostringstream out;
        out << "if (vfpu_prefixes_identity(state)) { "
            << "execute_vfpu_prefix_free<VfpuStaticOperation::" << operation_name
            << ", " << size << ">(state, " << vd << "U, " << vs << "U, " << vt
            << "U); note_vfpu_static_lowering(state); } else { "
            << "note_vfpu_helper_fallback(state); execute_vfpu(state, "
            << hex(instruction) << ", state.memory_base + " << hex(pc) << "); }";
        return out.str();
    }

    std::string emit_instruction(std::uint32_t pc, std::uint32_t instruction,
                                 bool relocated, std::uint32_t preferred_base)
    {
        const auto decoded = decode_allegrex(instruction);
        const auto op = decoded.op;
        const auto rs = decoded.rs;
        const auto rt = decoded.rt;
        const auto rd = decoded.rd;
        const auto shift = decoded.shift;
        const auto function = decoded.function;
        std::ostringstream out;
        if (!decoded.valid())
        {
            return "/* unsupported/reserved PSP word */ "
                   "state.stop_reason = StopReason::invalid_pc;";
        }
        if (decoded.lowering == InstructionLowering::guarded_native)
        {
            return emit_guarded_vfpu(pc, instruction,
                                     vfpu_static_operation(instruction));
        }
        if (decoded.lowering == InstructionLowering::runtime_fallback)
        {
            out << "execute_vfpu(state, " << hex(instruction)
                << ", state.memory_base + " << hex(pc) << ");";
            return out.str();
        }
        if (op == 0)
        {
            switch (function)
            {
            case 0x00:
                if (instruction == 0)
                {
                    out << "/* nop */";
                }
                else
                {
                    out << reg(rd) << " = " << reg(rt) << " << " << shift << "U;";
                }
                break;
            case 0x02:
                if (rs == 1)
                {
                    out << reg(rd) << " = rotate_right(" << reg(rt) << ", " << shift
                        << "U);";
                }
                else if (rs == 0)
                {
                    out << reg(rd) << " = " << reg(rt) << " >> " << shift << "U;";
                }
                else
                {
                    return "/* unsupported/reserved PSP word */ "
                           "state.stop_reason = StopReason::invalid_pc;";
                }
                break;
            case 0x03:
                out << reg(rd) << " = arithmetic_shift_right(" << reg(rt) << ", "
                    << shift << "U);";
                break;
            case 0x04:
                out << reg(rd) << " = " << reg(rt) << " << (" << reg(rs)
                    << " & 31U);";
                break;
            case 0x06:
                if (shift == 1)
                {
                    out << reg(rd) << " = rotate_right(" << reg(rt) << ", "
                        << reg(rs) << ");";
                }
                else if (shift == 0)
                {
                    out << reg(rd) << " = " << reg(rt) << " >> (" << reg(rs)
                        << " & 31U);";
                }
                else
                {
                    return "/* unsupported/reserved PSP word */ "
                           "state.stop_reason = StopReason::invalid_pc;";
                }
                break;
            case 0x07:
                out << reg(rd) << " = arithmetic_shift_right(" << reg(rt) << ", "
                    << reg(rs) << ");";
                break;
            case 0x08:
                out << "state.branch_pending = true; state.branch_target = "
                    << reg(rs) << ";";
                break;
            case 0x09:
                out << reg(rd) << " = state.memory_base + " << hex(pc + 8U)
                    << "; state.branch_pending = true; state.branch_target = "
                    << reg(rs) << ";";
                break;
            case 0x0a:
                out << "if (" << reg(rt) << " == 0) { " << reg(rd) << " = "
                    << reg(rs) << "; }";
                break;
            case 0x0b:
                out << "if (" << reg(rt) << " != 0) { " << reg(rd) << " = "
                    << reg(rs) << "; }";
                break;
            case 0x0c:
                out << "state.stop_reason = StopReason::syscall;";
                break;
            case 0x0d:
                // PPSSPP's default PSP compatibility behavior logs BREAK
                // exceptions and resumes at the following instruction.  Retail
                // games use this in fallback paths which still return an error to
                // their caller, so treating it as a terminal debugger stop can
                // kill an otherwise recoverable game thread.
                out << "/* break: ignored like PPSSPP's default exception policy */";
                break;
            case 0x0f:
                out << "/* sync: generated C++ executes in order */";
                break;
            case 0x10:
                out << reg(rd) << " = state.hi;";
                break;
            case 0x11:
                out << "state.hi = " << reg(rs) << ";";
                break;
            case 0x12:
                out << reg(rd) << " = state.lo;";
                break;
            case 0x13:
                out << "state.lo = " << reg(rs) << ";";
                break;
            case 0x16: // Allegrex clz
                out << reg(rd) << " = std::countl_zero(" << reg(rs) << ");";
                break;
            case 0x17: // Allegrex clo
                out << reg(rd) << " = std::countl_one(" << reg(rs) << ");";
                break;
            case 0x18:
                out << "{ const auto product = static_cast<std::int64_t>(as_s32("
                    << reg(rs) << ")) * static_cast<std::int64_t>(as_s32("
                    << reg(rt)
                    << ")); state.lo = static_cast<std::uint32_t>(product); "
                       "state.hi = "
                       "static_cast<std::uint32_t>(static_cast<std::uint64_t>("
                       "product) "
                       ">> "
                       "32U); }";
                break;
            case 0x19:
                out << "{ const auto product = static_cast<std::uint64_t>("
                    << reg(rs) << ") * static_cast<std::uint64_t>(" << reg(rt)
                    << "); state.lo = static_cast<std::uint32_t>(product); "
                       "state.hi = "
                       "static_cast<std::uint32_t>(product >> 32U); }";
                break;
            case 0x1a:
                out << "{ const auto dividend = as_s32(" << reg(rs)
                    << "); const auto divisor = as_s32(" << reg(rt)
                    << "); if (divisor == 0) { state.lo = dividend < 0 ? 1U : "
                       "0xffffffffU; state.hi = "
                       "static_cast<std::uint32_t>(dividend); } "
                       "else if (static_cast<std::uint32_t>(dividend) == "
                       "0x80000000U && "
                       "divisor == -1) { state.lo = 0x80000000U; state.hi = 0xffffffffU; } "
                       "else { "
                       "state.lo = static_cast<std::uint32_t>(dividend / divisor); "
                       "state.hi "
                       "= static_cast<std::uint32_t>(dividend % divisor); } }";
                break;
            case 0x1b:
                out << "{ const auto divisor = " << reg(rt)
                    << "; const auto dividend = " << reg(rs)
                    << "; if (divisor == 0) { state.lo = dividend <= 0xffffU ? 0xffffU : 0xffffffffU; state.hi = dividend; } else { state.lo = dividend / divisor; state.hi = dividend % divisor; } }";
                break;
            case 0x1c:
            { // madd
                out << "{ const auto product = static_cast<std::int64_t>(as_s32("
                    << reg(rs) << ")) * static_cast<std::int64_t>(as_s32("
                    << reg(rt)
                    << ")); const auto acc = static_cast<std::uint64_t>(state.lo) "
                       "| (static_cast<std::uint64_t>(state.hi) << 32U); "
                       "const auto result = static_cast<std::uint64_t>("
                       "static_cast<std::int64_t>(acc) + product); "
                       "state.lo = static_cast<std::uint32_t>(result); "
                       "state.hi = static_cast<std::uint32_t>(result >> 32U); }";
                break;
            }
            case 0x1d:
            { // maddu
                out << "{ const auto product = static_cast<std::uint64_t>("
                    << reg(rs) << ") * static_cast<std::uint64_t>(" << reg(rt)
                    << "); const auto acc = static_cast<std::uint64_t>(state.lo) "
                       "| (static_cast<std::uint64_t>(state.hi) << 32U); "
                       "const auto result = acc + product; "
                       "state.lo = static_cast<std::uint32_t>(result); "
                       "state.hi = static_cast<std::uint32_t>(result >> 32U); }";
                break;
            }
            case 0x20:
            case 0x21:
                out << reg(rd) << " = " << reg(rs) << " + " << reg(rt) << ";";
                break;
            case 0x22:
            case 0x23:
                out << reg(rd) << " = " << reg(rs) << " - " << reg(rt) << ";";
                break;
            case 0x24:
                out << reg(rd) << " = " << reg(rs) << " & " << reg(rt) << ";";
                break;
            case 0x25:
                out << reg(rd) << " = " << reg(rs) << " | " << reg(rt) << ";";
                break;
            case 0x26:
                out << reg(rd) << " = " << reg(rs) << " ^ " << reg(rt) << ";";
                break;
            case 0x27:
                out << reg(rd) << " = ~(" << reg(rs) << " | " << reg(rt) << ");";
                break;
            case 0x2a:
                out << reg(rd) << " = as_s32(" << reg(rs) << ") < as_s32("
                    << reg(rt) << ");";
                break;
            case 0x2b:
                out << reg(rd) << " = " << reg(rs) << " < " << reg(rt) << ";";
                break;
            case 0x2c: // Allegrex max
                out << reg(rd) << " = as_s32(" << reg(rs) << ") > as_s32("
                    << reg(rt) << ") ? " << reg(rs) << " : " << reg(rt) << ";";
                break;
            case 0x2d: // Allegrex min
                out << reg(rd) << " = as_s32(" << reg(rs) << ") < as_s32("
                    << reg(rt) << ") ? " << reg(rs) << " : " << reg(rt) << ";";
                break;
            case 0x2e:
            { // msub
                out << "{ const auto product = static_cast<std::int64_t>(as_s32("
                    << reg(rs) << ")) * static_cast<std::int64_t>(as_s32("
                    << reg(rt)
                    << ")); const auto acc = static_cast<std::uint64_t>(state.lo) "
                       "| (static_cast<std::uint64_t>(state.hi) << 32U); "
                       "const auto result = static_cast<std::uint64_t>("
                       "static_cast<std::int64_t>(acc) - product); "
                       "state.lo = static_cast<std::uint32_t>(result); "
                       "state.hi = static_cast<std::uint32_t>(result >> 32U); }";
                break;
            }
            case 0x2f:
            { // msubu
                out << "{ const auto product = static_cast<std::uint64_t>("
                    << reg(rs) << ") * static_cast<std::uint64_t>(" << reg(rt)
                    << "); const auto acc = static_cast<std::uint64_t>(state.lo) "
                       "| (static_cast<std::uint64_t>(state.hi) << 32U); "
                       "const auto result = acc - product; "
                       "state.lo = static_cast<std::uint32_t>(result); "
                       "state.hi = static_cast<std::uint32_t>(result >> 32U); }";
                break;
            }
            default:
                return "/* unsupported/reserved PSP word */ "
                       "state.stop_reason = StopReason::invalid_pc;";
            }
            return out.str();
        }

        switch (op)
        {
        case 0x01:
        {
            const bool ge = rt == 0x01 || rt == 0x03 || rt == 0x11 || rt == 0x13;
            const bool link = rt == 0x10 || rt == 0x11 || rt == 0x12 || rt == 0x13;
            const bool likely =
                rt == 0x02 || rt == 0x03 || rt == 0x12 || rt == 0x13;
            if (rt != 0x00 && rt != 0x01 && rt != 0x02 && rt != 0x03 &&
                rt != 0x10 && rt != 0x11 && rt != 0x12 && rt != 0x13)
            {
                out << "/* reserved REGIMM word in executable data */ "
                       "state.stop_reason = StopReason::invalid_pc;";
                break;
            }
            if (link)
            {
                out << "state.gpr[31] = state.memory_base + " << hex(pc + 8U)
                    << "; ";
            }
            out << "if (as_s32(" << reg(rs) << ") " << (ge ? ">= 0" : "< 0")
                << ") { state.branch_pending = true; state.branch_target = "
                << branch_target(pc, instruction, relocated) << "; }";
            if (likely)
            {
                out << " else { state.pc = state.memory_base + " << hex(pc + 8U)
                    << "; }";
            }
            break;
        }
        case 0x02:
        case 0x03:
        {
            if (op == 0x03)
            {
                out << "state.gpr[31] = state.memory_base + " << hex(pc + 8U)
                    << "; ";
            }
            out << "state.branch_pending = true; state.branch_target = "
                << jump_target(pc, instruction, relocated, preferred_base) << ";";
            break;
        }
        case 0x04:
            out << "if (" << reg(rs) << " == " << reg(rt)
                << ") { state.branch_pending = true; state.branch_target = "
                << branch_target(pc, instruction, relocated) << "; }";
            break;
        case 0x05:
            out << "if (" << reg(rs) << " != " << reg(rt)
                << ") { state.branch_pending = true; state.branch_target = "
                << branch_target(pc, instruction, relocated) << "; }";
            break;
        case 0x06:
            out << "if (as_s32(" << reg(rs)
                << ") <= 0) { state.branch_pending = true; state.branch_target = "
                << branch_target(pc, instruction, relocated) << "; }";
            break;
        case 0x07:
            out << "if (as_s32(" << reg(rs)
                << ") > 0) { state.branch_pending = true; state.branch_target = "
                << branch_target(pc, instruction, relocated) << "; }";
            break;
        case 0x14:
            out << "if (" << reg(rs) << " == " << reg(rt)
                << ") { state.branch_pending = true; state.branch_target = "
                << branch_target(pc, instruction, relocated)
                << "; } else { state.pc = state.memory_base + " << hex(pc + 8U)
                << "; }";
            break;
        case 0x15:
            out << "if (" << reg(rs) << " != " << reg(rt)
                << ") { state.branch_pending = true; state.branch_target = "
                << branch_target(pc, instruction, relocated)
                << "; } else { state.pc = state.memory_base + " << hex(pc + 8U)
                << "; }";
            break;
        case 0x16:
            out << "if (as_s32(" << reg(rs)
                << ") <= 0) { state.branch_pending = true; state.branch_target = "
                << branch_target(pc, instruction, relocated)
                << "; } else { state.pc = state.memory_base + " << hex(pc + 8U)
                << "; }";
            break;
        case 0x17:
            out << "if (as_s32(" << reg(rs)
                << ") > 0) { state.branch_pending = true; state.branch_target = "
                << branch_target(pc, instruction, relocated)
                << "; } else { state.pc = state.memory_base + " << hex(pc + 8U)
                << "; }";
            break;
        case 0x1c:
            switch (function)
            {
            case 0x20: // clz
                out << reg(rd) << " = std::countl_zero(" << reg(rs) << ");";
                break;
            case 0x21: // clo
                out << reg(rd) << " = std::countl_one(" << reg(rs) << ");";
                break;
            case 0x2c: // max
                out << reg(rd) << " = as_s32(" << reg(rs) << ") > as_s32("
                    << reg(rt) << ") ? " << reg(rs) << " : " << reg(rt) << ";";
                break;
            case 0x2d: // min
                out << reg(rd) << " = as_s32(" << reg(rs) << ") < as_s32("
                    << reg(rt) << ") ? " << reg(rs) << " : " << reg(rt) << ";";
                break;
            default:
                return "/* unsupported/reserved PSP word */ "
                       "state.stop_reason = StopReason::invalid_pc;";
            }
            break;
        case 0x08:
        case 0x09:
            out << reg(rt) << " = " << reg(rs) << signed_imm(instruction, relocated)
                << ";";
            break;
        case 0x0a:
            out << reg(rt) << " = as_s32(" << reg(rs) << ") < "
                << signed_immediate_s32(instruction, relocated) << ";";
            break;
        case 0x0b:
            out << reg(rt) << " = " << reg(rs) << " < "
                << signed_immediate(instruction, relocated) << ";";
            break;
        case 0x0c:
            out << reg(rt) << " = " << reg(rs) << " & "
                << immediate(instruction, relocated) << ";";
            break;
        case 0x0d:
            out << reg(rt) << " = " << reg(rs) << " | "
                << immediate(instruction, relocated) << ";";
            break;
        case 0x0e:
            out << reg(rt) << " = " << reg(rs) << " ^ "
                << immediate(instruction, relocated) << ";";
            break;
        case 0x0f:
            out << reg(rt) << " = " << immediate(instruction, relocated)
                << " << 16U;";
            break;
        case 0x11:
        {
            const auto fmt = rs;
            const auto ft = rt;
            const auto fs = rd;
            const auto fd = shift;
            if (fmt == 0x00)
            {
                out << reg(rt) << " = state.fpr[" << fs << "];";
            }
            else if (fmt == 0x02)
            {
                if (fs == 0)
                {
                    out << reg(rt) << " = 0x00003351U;";
                }
                else if (fs == 31)
                {
                    out << reg(rt) << " = state.fcr31;";
                }
                else
                {
                    out << reg(rt) << " = 0;";
                }
            }
            else if (fmt == 0x04)
            {
                out << "state.fpr[" << fs << "] = " << reg(rt) << ";";
            }
            else if (fmt == 0x06)
            {
                if (fs == 31)
                {
                    out << "state.fcr31 = " << reg(rt) << ";";
                }
                else
                {
                    out << "/* write to unimplemented FCR ignored */";
                }
            }
            else if (fmt == 0x08)
            {
                const auto cc = (rt >> 2U) & 7U;
                const bool branch_true = (rt & 1U) != 0U;
                const bool likely = (rt & 2U) != 0U;
                out << "if (fpu_condition(state, " << cc
                    << "U) == " << (branch_true ? "true" : "false")
                    << ") { state.branch_pending = true; state.branch_target = "
                    << branch_target(pc, instruction, relocated) << "; }";
                if (likely)
                {
                    out << " else { state.pc = state.memory_base + " << hex(pc + 8U)
                        << "; }";
                }
            }
            else if (fmt == 0x10)
            {
                switch (function)
                {
                case 0x00:
                    out << "set_f32(state, " << fd << ", f32(state, " << fs
                        << ") + f32(state, " << ft << "));";
                    break;
                case 0x01:
                    out << "set_f32(state, " << fd << ", f32(state, " << fs
                        << ") - f32(state, " << ft << "));";
                    break;
                case 0x02:
                    out << "set_f32(state, " << fd << ", f32(state, " << fs
                        << ") * f32(state, " << ft << "));";
                    break;
                case 0x03:
                    out << "set_f32(state, " << fd << ", f32(state, " << fs
                        << ") / f32(state, " << ft << "));";
                    break;
                case 0x04:
                    out << "set_f32(state, " << fd << ", std::sqrt(f32(state, "
                        << fs << ")));";
                    break;
                case 0x05:
                    out << "state.fpr[" << fd << "] = state.fpr[" << fs
                        << "] & 0x7fffffffU;";
                    break;
                case 0x06:
                    out << "state.fpr[" << fd << "] = state.fpr[" << fs << "];";
                    break;
                case 0x07:
                    out << "state.fpr[" << fd << "] = state.fpr[" << fs
                        << "] ^ 0x80000000U;";
                    break;
                case 0x0c:
                    out << "state.fpr[" << fd << "] = rounded_word(f32(state, "
                        << fs << "), 0);";
                    break;
                case 0x0d:
                    out << "state.fpr[" << fd << "] = rounded_word(f32(state, "
                        << fs << "), 1);";
                    break;
                case 0x0e:
                    out << "state.fpr[" << fd << "] = rounded_word(f32(state, "
                        << fs << "), 2);";
                    break;
                case 0x0f:
                    out << "state.fpr[" << fd << "] = rounded_word(f32(state, "
                        << fs << "), 3);";
                    break;
                case 0x11:
                {
                    const auto cc = (ft >> 2U) & 7U;
                    const bool move_true = (ft & 1U) != 0U;
                    out << "if (fpu_condition(state, " << cc
                        << "U) == " << (move_true ? "true" : "false")
                        << ") { state.fpr[" << fd << "] = state.fpr[" << fs
                        << "]; }";
                    break;
                }
                case 0x20:
                    out << "state.fpr[" << fd << "] = state.fpr[" << fs << "];";
                    break;
                case 0x24:
                    out << "state.fpr[" << fd << "] = rounded_word(f32(state, "
                        << fs << "), state.fcr31 & 3U);";
                    break;
                default:
                    if (function >= 0x30)
                    {
                        const auto cc = (instruction >> 8U) & 7U;
                        out << "compare_f32(state, " << cc << "U, f32(state, " << fs
                            << "), f32(state, " << ft << "), " << (function & 15U)
                            << "U);";
                    }
                    else
                    {
                        return "/* unsupported/reserved PSP word */ "
                               "state.stop_reason = StopReason::invalid_pc;";
                    }
                    break;
                }
            }
            else if (fmt == 0x14 && function == 0x20)
            {
                out << "set_f32(state, " << fd << ", static_cast<float>(as_s32("
                    << "state.fpr[" << fs << "])));";
            }
            else
            {
                return "/* unsupported/reserved PSP word */ "
                       "state.stop_reason = StopReason::invalid_pc;";
            }
            break;
        }
        case 0x20:
            out << reg(rt)
                << " = static_cast<std::uint32_t>(static_cast<std::int32_t>("
                   "static_cast<std::int8_t>(PSPRECOMP_LOAD8(state, "
                << reg(rs) << signed_imm(instruction, relocated) << "))));";
            break;
        case 0x21:
            out << reg(rt)
                << " = static_cast<std::uint32_t>(static_cast<std::int32_t>("
                   "static_cast<std::int16_t>(PSPRECOMP_LOAD16(state, "
                << reg(rs) << signed_imm(instruction, relocated) << "))));";
            break;
        case 0x22:
            out << reg(rt) << " = load_word_left(state, " << reg(rs)
                << signed_imm(instruction, relocated) << ", " << reg(rt) << ");";
            break;
        case 0x23:
            out << reg(rt) << " = PSPRECOMP_LOAD32(state, " << reg(rs)
                << signed_imm(instruction, relocated) << ");";
            break;
        case 0x24:
            out << reg(rt) << " = PSPRECOMP_LOAD8(state, " << reg(rs)
                << signed_imm(instruction, relocated) << ");";
            break;
        case 0x25:
            out << reg(rt) << " = PSPRECOMP_LOAD16(state, " << reg(rs)
                << signed_imm(instruction, relocated) << ");";
            break;
        case 0x26:
            out << reg(rt) << " = load_word_right(state, " << reg(rs)
                << signed_imm(instruction, relocated) << ", " << reg(rt) << ");";
            break;
        case 0x28:
            out << "PSPRECOMP_STORE8(state, " << reg(rs)
                << signed_imm(instruction, relocated)
                << ", static_cast<std::uint8_t>(" << reg(rt) << "));";
            break;
        case 0x29:
            out << "PSPRECOMP_STORE16(state, " << reg(rs)
                << signed_imm(instruction, relocated)
                << ", static_cast<std::uint16_t>(" << reg(rt) << "));";
            break;
        case 0x2a:
            out << "store_word_left(state, " << reg(rs)
                << signed_imm(instruction, relocated) << ", " << reg(rt) << ");";
            break;
        case 0x2b:
            out << "PSPRECOMP_STORE32(state, " << reg(rs)
                << signed_imm(instruction, relocated) << ", " << reg(rt) << ");";
            break;
        case 0x2e:
            out << "store_word_right(state, " << reg(rs)
                << signed_imm(instruction, relocated) << ", " << reg(rt) << ");";
            break;
        case 0x2f:
            out << "/* cache: no generated-runtime cache */";
            break;
        case 0x30: // ll - load linked (treat as lw on PSP user-mode)
            out << reg(rt) << " = PSPRECOMP_LOAD32(state, " << reg(rs)
                << signed_imm(instruction, relocated) << ");";
            break;
        case 0x38: // sc - store conditional (always succeeds on PSP user-mode)
            out << "PSPRECOMP_STORE32(state, " << reg(rs)
                << signed_imm(instruction, relocated) << ", " << reg(rt) << "); "
                << reg(rt) << " = 1U;";
            break;
        case 0x31:
            out << "state.fpr[" << rt << "] = PSPRECOMP_LOAD32(state, " << reg(rs)
                << signed_imm(instruction, relocated) << ");";
            break;
        case 0x39:
            out << "PSPRECOMP_STORE32(state, " << reg(rs)
                << signed_imm(instruction, relocated) << ", state.fpr[" << rt
                << "]);";
            break;
        case 0x1f:
        {
            if (function == 0x20 && shift == 0x14)
            {
                // bitrev
                out << reg(rd) << " = reverse_bits(" << reg(rt) << ");";
                break;
            }
            if (function == 0x20 && shift == 0x02)
            {
                // wsbh - swap bytes within halfwords
                out << reg(rd) << " = ((" << reg(rt)
                    << " & 0xff00ff00U) >> 8U) | ((" << reg(rt)
                    << " & 0x00ff00ffU) << 8U);";
                break;
            }
            if (function == 0x20 && shift == 0x03)
            {
                // wsbw - full byte swap
                out << reg(rd) << " = byte_swap(" << reg(rt) << ");";
                break;
            }
            if (function == 0x20 && shift == 0x10)
            {
                out << reg(rd)
                    << " = static_cast<std::uint32_t>(static_cast<std::int32_t>("
                       "static_cast<std::int8_t>("
                    << reg(rt) << ")));";
                break;
            }
            if (function == 0x20 && shift == 0x18)
            {
                out << reg(rd)
                    << " = static_cast<std::uint32_t>(static_cast<std::int32_t>("
                       "static_cast<std::int16_t>("
                    << reg(rt) << ")));";
                break;
            }
            if (function != 0)
            {
                if (function == 4 && rd >= shift)
                {
                    const auto position = shift;
                    const auto width = rd - shift + 1U;
                    const auto mask = width == 32 ? 0xffffffffU
                                                  : (1U << width) - 1U;
                    const auto positioned_mask = mask << position;
                    out << reg(rt) << " = (" << reg(rt) << " & ~"
                        << hex(positioned_mask) << ") | ((" << reg(rs) << " << "
                        << position << "U) & " << hex(positioned_mask) << ");";
                    break;
                }
                return "/* unsupported/reserved PSP word */ "
                       "state.stop_reason = StopReason::invalid_pc;";
            }
            const auto position = shift;
            const auto width = rd + 1U;
            const auto mask = width == 32 ? 0xffffffffU : (1U << width) - 1U;
            out << reg(rt) << " = (" << reg(rs) << " >> " << position << "U) & "
                << hex(mask) << ";";
            break;
        }
        default:
            return "/* unsupported/reserved PSP word */ "
                   "state.stop_reason = StopReason::invalid_pc;";
        }
        return out.str();
    }

} // namespace psprecomp::detail
