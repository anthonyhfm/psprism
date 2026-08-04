#pragma once

#include <psprecomp/runtime.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace psprecomp {

inline constexpr bool vfpu_opcode_supported(std::uint32_t instruction) {
    const auto op = instruction >> 26U;
    const auto sub3 = (instruction >> 23U) & 7U;
    if (op == 0x12) {
        const auto rs = (instruction >> 21U) & 31U;
        return rs == 3 || rs == 7 || rs == 8;
    }
    if (op == 0x18) {
        return sub3 == 0 || sub3 == 1;
    }
    if (op == 0x19) {
        return sub3 <= 2 ||
               (instruction & 0xff808080U) == 0x66808000U;
    }
    if (op == 0x1b) {
        return sub3 == 0 || sub3 == 2 || sub3 == 3 ||
               (instruction & 0xff808080U) == 0x6e808000U ||
               (instruction & 0xff808080U) == 0x6f808080U;
    }
    if (op == 0x32 || op == 0x35 || op == 0x36 || op == 0x3a ||
        op == 0x3d || op == 0x3e || op == 0x3f) {
        return true;
    }
    if (op == 0x37) {
        return sub3 <= 5 || sub3 == 7;
    }
    if (op == 0x34) {
        const auto group = (instruction >> 21U) & 31U;
        const auto type = (instruction >> 16U) & 31U;
        if (group == 0) {
            return type == 0 || type == 1 || type == 2 || type == 4 ||
                   type == 5 || type == 6 ||
                   type == 7 || (type >= 16 && type <= 22);
        }
        if (group == 1) {
            return type >= 24;
        }
        if (group == 2) {
            return type == 1 || type == 6 || type == 9 || type == 10 ||
                   type == 25;
        }
        if (group == 3) {
            return type <= 19;
        }
        return group >= 16 && group <= 20;
    }
    if (op == 0x3c) {
        const auto group = (instruction >> 21U) & 31U;
        return group <= 3 || (group >= 12 && group <= 15) ||
               (group >= 20 && group <= 23) ||
               (instruction & 0xff808080U) == 0xf1008000U ||
               (instruction & 0xff808080U) == 0xf2008080U ||
               (instruction & 0xffff0000U) == 0xf3800000U ||
               (instruction & 0xffffff80U) == 0xf3838080U ||
               (instruction & 0xffffff80U) == 0xf3868080U;
    }
    return false;
}

inline int vfpu_size(std::uint32_t instruction) {
    return static_cast<int>(((instruction >> 7U) & 1U) +
                            ((instruction >> 14U) & 2U) + 1U);
}

inline std::uint32_t vfpu_index(std::uint32_t reg, int size, int lane = 0) {
    const auto matrix = (reg >> 2U) & 7U;
    const auto column = reg & 3U;
    std::uint32_t row{};
    if (size == 1) {
        row = (reg >> 5U) & 3U;
    } else if (size == 2 || size == 4) {
        row = (reg >> 5U) & 2U;
    } else {
        row = (reg >> 6U) & 1U;
    }
    const bool transpose = size != 1 && ((reg >> 5U) & 1U) != 0U;
    if (transpose) {
        return matrix * 4U + ((row + static_cast<std::uint32_t>(lane)) & 3U) +
               column * 32U;
    }
    return matrix * 4U + column +
           ((row + static_cast<std::uint32_t>(lane)) & 3U) * 32U;
}

inline float vfpu_float(const State& state, std::uint32_t index) {
    return std::bit_cast<float>(state.vfpu[index & 127U]);
}

inline void vfpu_read_vector(const State& state, std::uint32_t reg, int size,
                             float values[4]) {
    for (int lane = 0; lane < size; ++lane) {
        values[lane] = vfpu_float(state, vfpu_index(reg, size, lane));
    }
    for (int lane = size; lane < 4; ++lane) {
        values[lane] = 0.0F;
    }
}

inline void vfpu_write_vector(State& state, std::uint32_t reg, int size,
                              const float values[4]) {
    for (int lane = 0; lane < size; ++lane) {
        if ((state.vfpu_ctrl[2] & (1U << (8 + lane))) == 0U) {
            state.vfpu[vfpu_index(reg, size, lane)] =
                std::bit_cast<std::uint32_t>(values[lane]);
        }
    }
}

inline void vfpu_apply_prefix(float values[4], int size, std::uint32_t prefix) {
    if (prefix == 0xe4U) {
        return;
    }
    const float constants[8] = {0.0F, 1.0F, 2.0F, 0.5F,
                                3.0F, 1.0F / 3.0F, 0.25F, 1.0F / 6.0F};
    float original[4]{};
    for (int i = 0; i < size; ++i) {
        original[i] = values[i];
    }
    for (int i = 0; i < size; ++i) {
        const auto swizzle = (prefix >> (i * 2)) & 3U;
        const bool absolute = (prefix & (1U << (8 + i))) != 0U;
        const bool constant = (prefix & (1U << (12 + i))) != 0U;
        const bool negate = (prefix & (1U << (16 + i))) != 0U;
        if (constant) {
            values[i] = constants[swizzle + (absolute ? 4U : 0U)];
        } else {
            values[i] = swizzle < static_cast<std::uint32_t>(size)
                            ? original[swizzle]
                            : 0.0F;
            if (absolute) {
                values[i] = std::fabs(values[i]);
            }
        }
        if (negate) {
            values[i] = -values[i];
        }
    }
}

inline void vfpu_apply_destination_prefix(State& state, float values[4],
                                          int size, bool saturation = true) {
    if (!saturation) {
        return;
    }
    for (int i = 0; i < size; ++i) {
        const auto mode = (state.vfpu_ctrl[2] >> (i * 2)) & 3U;
        if (mode == 1) {
            values[i] = std::clamp(values[i], 0.0F, 1.0F);
        } else if (mode == 3) {
            values[i] = std::clamp(values[i], -1.0F, 1.0F);
        }
    }
}

inline void vfpu_eat_prefixes(State& state) {
    state.vfpu_ctrl[0] = 0xe4U;
    state.vfpu_ctrl[1] = 0xe4U;
    state.vfpu_ctrl[2] = 0;
}

inline void vfpu_read_matrix(const State& state, std::uint32_t reg, int side,
                             float values[16]) {
    const auto matrix = (reg >> 2U) & 7U;
    const auto column = reg & 3U;
    const auto row = side == 3 ? (reg >> 6U) & 1U : (reg >> 5U) & 2U;
    const bool transpose = ((reg >> 5U) & 1U) != 0U;
    for (int j = 0; j < side; ++j) {
        for (int i = 0; i < side; ++i) {
            auto index = matrix * 4U;
            if (transpose) {
                index += ((row + static_cast<unsigned>(i)) & 3U) +
                         ((column + static_cast<unsigned>(j)) & 3U) * 32U;
            } else {
                index += ((column + static_cast<unsigned>(j)) & 3U) +
                         ((row + static_cast<unsigned>(i)) & 3U) * 32U;
            }
            values[j * 4 + i] = vfpu_float(state, index);
        }
    }
}

inline void vfpu_write_matrix(State& state, std::uint32_t reg, int side,
                              const float values[16]) {
    const auto matrix = (reg >> 2U) & 7U;
    const auto column = reg & 3U;
    const auto row = side == 3 ? (reg >> 6U) & 1U : (reg >> 5U) & 2U;
    const bool transpose = ((reg >> 5U) & 1U) != 0U;
    for (int j = 0; j < side; ++j) {
        for (int i = 0; i < side; ++i) {
            auto index = matrix * 4U;
            if (transpose) {
                index += ((row + static_cast<unsigned>(i)) & 3U) +
                         ((column + static_cast<unsigned>(j)) & 3U) * 32U;
            } else {
                index += ((column + static_cast<unsigned>(j)) & 3U) +
                         ((row + static_cast<unsigned>(i)) & 3U) * 32U;
            }
            state.vfpu[index] = std::bit_cast<std::uint32_t>(values[j * 4 + i]);
        }
    }
}

inline float vfpu_half_to_float(std::uint16_t value) {
    const auto sign = static_cast<std::uint32_t>(value & 0x8000U) << 16U;
    const auto exponent = (value >> 10U) & 31U;
    const auto fraction = value & 0x3ffU;
    std::uint32_t bits{};
    if (exponent == 0) {
        if (fraction == 0) {
            bits = sign;
        } else {
            auto mantissa = static_cast<std::uint32_t>(fraction);
            int shift = 0;
            while ((mantissa & 0x400U) == 0U) {
                mantissa <<= 1U;
                ++shift;
            }
            mantissa &= 0x3ffU;
            bits = sign | static_cast<std::uint32_t>(127 - 14 - shift) << 23U |
                   mantissa << 13U;
        }
    } else if (exponent == 31) {
        bits = sign | 0x7f800000U | static_cast<std::uint32_t>(fraction) << 13U;
    } else {
        bits = sign | static_cast<std::uint32_t>(exponent + 112U) << 23U |
               static_cast<std::uint32_t>(fraction) << 13U;
    }
    return std::bit_cast<float>(bits);
}

inline void execute_vfpu(State& state, std::uint32_t instruction,
                         std::uint32_t current_pc) {
    const auto op = instruction >> 26U;
    const auto vd = instruction & 0x7fU;
    const auto vs = (instruction >> 8U) & 0x7fU;
    const auto vt = (instruction >> 16U) & 0x7fU;
    const int size = vfpu_size(instruction);

    if (op == 0x12) {
        const auto kind = (instruction >> 21U) & 31U;
        const auto rt = (instruction >> 16U) & 31U;
        const auto immediate = instruction & 0xffU;
        if (kind == 3) {
            if (rt != 0) {
                state.gpr[rt] = immediate < 128
                                    ? state.vfpu[vfpu_index(immediate, 1)]
                                    : state.vfpu_ctrl[(immediate - 128U) & 15U];
            }
        } else if (kind == 7) {
            if (immediate < 128) {
                state.vfpu[vfpu_index(immediate, 1)] = state.gpr[rt];
            } else {
                state.vfpu_ctrl[(immediate - 128U) & 15U] = state.gpr[rt];
            }
        } else {
            const auto cc = (instruction >> 18U) & 7U;
            const bool expected = ((instruction >> 16U) & 1U) != 0U;
            const bool likely = ((instruction >> 17U) & 1U) != 0U;
            const bool condition = (state.vfpu_ctrl[3] & (1U << cc)) != 0U;
            if (condition == expected) {
                const auto displacement = static_cast<std::int32_t>(
                                              static_cast<std::int16_t>(instruction)) *
                                          4;
                state.branch_pending = true;
                state.branch_target = current_pc + 4U +
                                      static_cast<std::uint32_t>(displacement);
            } else if (likely) {
                state.pc = current_pc + 8U;
            }
        }
        return;
    }

    if (op == 0x32 || op == 0x3a) {
        const auto base = (instruction >> 21U) & 31U;
        const auto reg = ((instruction >> 16U) & 31U) | ((instruction & 3U) << 5U);
        const auto offset = static_cast<std::int32_t>(
            static_cast<std::int16_t>(instruction & 0xfffcU));
        const auto address = state.gpr[base] + static_cast<std::uint32_t>(offset);
        if (op == 0x32) {
            state.vfpu[vfpu_index(reg, 1)] = PSPRECOMP_LOAD32(state, address);
        } else {
            PSPRECOMP_STORE32(state, address, state.vfpu[vfpu_index(reg, 1)]);
        }
        return;
    }
    if (op == 0x35 || op == 0x3d) {
        const auto base = (instruction >> 21U) & 31U;
        const auto reg = ((instruction >> 16U) & 31U) |
                         ((instruction & 1U) << 5U);
        const auto offset = static_cast<std::int32_t>(
            static_cast<std::int16_t>(instruction & 0xfffcU));
        const auto address = state.gpr[base] + static_cast<std::uint32_t>(offset);
        const auto word_offset = (address >> 2U) & 3U;
        float values[4]{};
        vfpu_read_vector(state, reg, 4, values);
        if (op == 0x35) {
            if ((instruction & 2U) == 0U) {
                for (std::uint32_t i = 0; i <= word_offset; ++i) {
                    values[3U - i] = std::bit_cast<float>(
                        PSPRECOMP_LOAD32(state, address - i * 4U));
                }
            } else {
                for (std::uint32_t i = 0; i <= 3U - word_offset; ++i) {
                    values[i] = std::bit_cast<float>(
                        PSPRECOMP_LOAD32(state, address + i * 4U));
                }
            }
            vfpu_write_vector(state, reg, 4, values);
        } else if ((instruction & 2U) == 0U) {
            for (std::uint32_t i = 0; i <= word_offset; ++i) {
                PSPRECOMP_STORE32(state, address - i * 4U,
                                  std::bit_cast<std::uint32_t>(values[3U - i]));
            }
        } else {
            for (std::uint32_t i = 0; i <= 3U - word_offset; ++i) {
                PSPRECOMP_STORE32(state, address + i * 4U,
                                  std::bit_cast<std::uint32_t>(values[i]));
            }
        }
        return;
    }
    if (op == 0x36 || op == 0x3e) {
        const auto base = (instruction >> 21U) & 31U;
        const auto reg = ((instruction >> 16U) & 31U) | ((instruction & 1U) << 5U);
        const auto offset = static_cast<std::int32_t>(
            static_cast<std::int16_t>(instruction & 0xfffcU));
        const auto address = state.gpr[base] + static_cast<std::uint32_t>(offset);
        float values[4]{};
        if (op == 0x36) {
            for (int i = 0; i < 4; ++i) {
                values[i] = std::bit_cast<float>(
                    PSPRECOMP_LOAD32(state, address + i * 4U));
            }
            vfpu_write_vector(state, reg, 4, values);
        } else {
            vfpu_read_vector(state, reg, 4, values);
            for (int i = 0; i < 4; ++i) {
                PSPRECOMP_STORE32(state, address + i * 4U,
                                  std::bit_cast<std::uint32_t>(values[i]));
            }
        }
        return;
    }
    if (op == 0x3f) {
        return;
    }
    if (op == 0x37) {
        const auto type = (instruction >> 23U) & 7U;
        if (type <= 5) {
            auto index = (instruction >> 24U) & 3U;
            auto data = instruction & 0xfffffU;
            if (index == 2) {
                data &= 0xfffU;
            }
            state.vfpu_ctrl[index] = data;
        } else {
            float value = vfpu_half_to_float(static_cast<std::uint16_t>(instruction));
            float values[4] = {value, 0, 0, 0};
            vfpu_apply_destination_prefix(state, values, 1);
            vfpu_write_vector(state, vt, 1, values);
            vfpu_eat_prefixes(state);
        }
        return;
    }

    float source[4]{}, target[4]{}, result[4]{};
    vfpu_read_vector(state, vs, size, source);
    vfpu_read_vector(state, vt, size, target);
    vfpu_apply_prefix(source, size, state.vfpu_ctrl[0]);
    vfpu_apply_prefix(target, size, state.vfpu_ctrl[1]);

    if (op == 0x18) {
        const bool subtract = ((instruction >> 23U) & 7U) == 1U;
        for (int i = 0; i < size; ++i) {
            result[i] = subtract ? source[i] - target[i] : source[i] + target[i];
        }
    } else if (op == 0x19) {
        const auto type = (instruction >> 23U) & 7U;
        if ((instruction & 0xff808080U) == 0x66808000U) {
            result[0] = source[1] * target[2];
            result[1] = source[2] * target[0];
            result[2] = source[0] * target[1];
        } else if (type == 0) {
            for (int i = 0; i < size; ++i) {
                result[i] = source[i] * target[i];
            }
        } else if (type == 1) {
            for (int i = 0; i < size; ++i) {
                result[0] += source[i] * target[i];
            }
            vfpu_apply_destination_prefix(state, result, 1);
            vfpu_write_vector(state, vd, 1, result);
            vfpu_eat_prefixes(state);
            return;
        } else {
            float scalar[4]{};
            vfpu_read_vector(state, vt, 1, scalar);
            vfpu_apply_prefix(scalar, 1, state.vfpu_ctrl[1]);
            for (int i = 0; i < size; ++i) {
                result[i] = source[i] * scalar[0];
            }
        }
    } else if (op == 0x1b) {
        const auto type = (instruction >> 23U) & 7U;
        if ((instruction & 0xff808080U) == 0x6e808000U) {
            for (int i = 0; i < size; ++i) {
                result[i] = source[i] > target[i] ? 1.0F
                            : source[i] < target[i] ? -1.0F
                                                    : 0.0F;
            }
        } else if ((instruction & 0xff808080U) == 0x6f808080U) {
            for (int i = 0; i < size; ++i) {
                result[i] = source[i] < target[i] ? 1.0F : 0.0F;
            }
        } else if (type == 0) {
            const auto condition_code = instruction & 15U;
            std::uint32_t cc = 0;
            bool any = false;
            bool all = true;
            for (int i = 0; i < size; ++i) {
                bool condition{};
                switch (condition_code) {
                case 0: condition = false; break;
                case 1: condition = source[i] == target[i]; break;
                case 2: condition = source[i] < target[i]; break;
                case 3: condition = source[i] <= target[i]; break;
                case 4: condition = true; break;
                case 5: condition = source[i] != target[i]; break;
                case 6: condition = source[i] >= target[i]; break;
                case 7: condition = source[i] > target[i]; break;
                case 8: condition = source[i] == 0.0F; break;
                case 9: condition = std::isnan(source[i]); break;
                case 10: condition = std::isinf(source[i]); break;
                case 11: condition = !std::isfinite(source[i]); break;
                case 12: condition = source[i] != 0.0F; break;
                case 13: condition = !std::isnan(source[i]); break;
                case 14: condition = !std::isinf(source[i]); break;
                default: condition = std::isfinite(source[i]); break;
                }
                cc |= static_cast<std::uint32_t>(condition) << i;
                any = any || condition;
                all = all && condition;
            }
            const auto affected = ((1U << size) - 1U) | 0x30U;
            cc |= static_cast<std::uint32_t>(any) << 4U;
            cc |= static_cast<std::uint32_t>(all) << 5U;
            state.vfpu_ctrl[3] = (state.vfpu_ctrl[3] & ~affected) | (cc & affected);
            vfpu_eat_prefixes(state);
            return;
        }
        for (int i = 0; i < size; ++i) {
            result[i] = type == 2 ? std::min(source[i], target[i])
                                  : std::max(source[i], target[i]);
        }
    } else if (op == 0x34) {
        const auto group = (instruction >> 21U) & 31U;
        const auto type = (instruction >> 16U) & 31U;
        if (group == 3 && type <= 19) {
            constexpr std::array<float, 20> constants = {
                0.0F,
                std::numeric_limits<float>::max(),
                1.4142135623730951F,
                0.7071067811865476F,
                1.1283791670955126F,
                0.6366197723675813F,
                0.3183098861837907F,
                0.7853981633974483F,
                1.5707963267948966F,
                3.1415926535897932F,
                2.7182818284590452F,
                1.4426950408889634F,
                0.4342944819032518F,
                0.6931471805599453F,
                2.302585092994046F,
                6.2831853071795865F,
                0.5235987755982989F,
                0.3010299956639812F,
                3.3219280948873623F,
                0.8660254037844386F,
            };
            for (int i = 0; i < size; ++i) {
                result[i] = constants[type];
            }
        } else if (group == 2 && type == 10) {
            for (int i = 0; i < size; ++i) {
                result[i] = source[i] > 0.0F ? 1.0F
                            : source[i] < 0.0F ? -1.0F
                                               : 0.0F;
            }
        } else if (group == 2 && type == 6) {
            for (int i = 0; i < size; ++i) {
                result[0] += source[i];
            }
            vfpu_apply_destination_prefix(state, result, 1);
            vfpu_write_vector(state, vd, 1, result);
            vfpu_eat_prefixes(state);
            return;
        } else if (group == 2 && type == 1) {
            result[0] = std::min(source[0], source[3]);
            result[1] = std::min(source[1], source[2]);
            result[2] = std::max(source[1], source[2]);
            result[3] = std::max(source[0], source[3]);
        } else if (group == 2 && type == 9) {
            result[0] = std::max(source[0], source[3]);
            result[1] = std::max(source[1], source[2]);
            result[2] = std::min(source[1], source[2]);
            result[3] = std::min(source[0], source[3]);
        } else if (group == 1 && (type == 26 || type == 27)) {
            const auto input_size = std::min(size, 2);
            const auto output_size = input_size * 2;
            for (int i = 0; i < input_size; ++i) {
                const auto packed = std::bit_cast<std::uint32_t>(source[i]);
                if (type == 26) {
                    result[i * 2] = std::bit_cast<float>((packed & 0xffffU) << 15U);
                    result[i * 2 + 1] =
                        std::bit_cast<float>((packed & 0xffff0000U) >> 1U);
                } else {
                    result[i * 2] = std::bit_cast<float>((packed & 0xffffU) << 16U);
                    result[i * 2 + 1] =
                        std::bit_cast<float>(packed & 0xffff0000U);
                }
            }
            vfpu_apply_destination_prefix(state, result, output_size, false);
            vfpu_write_vector(state, vd, output_size, result);
            vfpu_eat_prefixes(state);
            return;
        } else if (group == 1 && (type == 30 || type == 31)) {
            const auto output_size = (size + 1) / 2;
            for (int i = 0; i < output_size; ++i) {
                const auto low_bits = std::bit_cast<std::uint32_t>(source[i * 2]);
                const auto high_bits = i * 2 + 1 < size
                                           ? std::bit_cast<std::uint32_t>(
                                                 source[i * 2 + 1])
                                           : 0U;
                std::uint32_t low{};
                std::uint32_t high{};
                if (type == 30) {
                    low = std::bit_cast<std::int32_t>(low_bits) < 0
                              ? 0U
                              : low_bits >> 15U;
                    high = std::bit_cast<std::int32_t>(high_bits) < 0
                               ? 0U
                               : high_bits >> 15U;
                } else {
                    low = low_bits >> 16U;
                    high = high_bits >> 16U;
                }
                result[i] = std::bit_cast<float>(low | (high << 16U));
            }
            vfpu_apply_destination_prefix(state, result, output_size, false);
            vfpu_write_vector(state, vd, output_size, result);
            vfpu_eat_prefixes(state);
            return;
        } else if (group == 1 && (type == 24 || type == 25)) {
            const auto packed = std::bit_cast<std::uint32_t>(source[0]);
            for (int i = 0; i < 4; ++i) {
                const auto byte = (packed >> (i * 8)) & 0xffU;
                const auto expanded = type == 24
                                          ? (byte * 0x01010101U) >> 1U
                                          : byte << 24U;
                result[i] = std::bit_cast<float>(expanded);
            }
            vfpu_apply_destination_prefix(state, result, 4, false);
            vfpu_write_vector(state, vd, 4, result);
            vfpu_eat_prefixes(state);
            return;
        } else if (group == 0) {
            for (int i = 0; i < size; ++i) {
                if (type == 0) result[i] = source[i];
                else if (type == 1) result[i] = std::fabs(source[i]);
                else if (type == 2) result[i] = -source[i];
                else if (type == 4) result[i] = std::clamp(source[i], 0.0F, 1.0F);
                else if (type == 5) result[i] = std::clamp(source[i], -1.0F, 1.0F);
                else if (type == 6) result[i] = 0.0F;
                else if (type == 7) result[i] = 1.0F;
                else if (type == 16) result[i] = 1.0F / source[i];
                else if (type == 17) result[i] = 1.0F / std::sqrt(source[i]);
                else if (type == 18) result[i] = std::sin(source[i] * 1.5707963267948966F);
                else if (type == 19) result[i] = std::cos(source[i] * 1.5707963267948966F);
                else if (type == 20) result[i] = std::exp2(source[i]);
                else if (type == 21) result[i] = std::log2(source[i]);
                else if (type == 22) result[i] = std::sqrt(source[i]);
            }
        } else if (group >= 16 && group <= 19) {
            const auto scale = std::ldexp(1.0F, static_cast<int>(type));
            for (int i = 0; i < size; ++i) {
                const auto scaled = std::trunc(static_cast<double>(source[i]) * scale);
                const auto value = std::clamp(scaled, -2147483648.0, 2147483647.0);
                result[i] = std::bit_cast<float>(
                    static_cast<std::uint32_t>(static_cast<std::int32_t>(value)));
            }
            vfpu_apply_destination_prefix(state, result, size, false);
        } else if (group == 20) {
            const auto scale = std::ldexp(1.0F, -static_cast<int>(type));
            for (int i = 0; i < size; ++i) {
                result[i] = static_cast<float>(
                                std::bit_cast<std::int32_t>(source[i])) *
                            scale;
            }
        } else if (group == 1 && (type == 28 || type == 29)) {
            std::uint32_t packed = 0;
            for (int i = 0; i < 4; ++i) {
                const auto bits = std::bit_cast<std::uint32_t>(source[i]);
                auto byte = type == 28
                                ? static_cast<std::int32_t>(bits) < 0
                                      ? 0U
                                      : (bits >> 23U) & 0xffU
                                : bits >> 24U;
                packed |= (byte & 0xffU) << (i * 8);
            }
            result[0] = std::bit_cast<float>(packed);
            vfpu_apply_destination_prefix(state, result, 1, false);
            vfpu_write_vector(state, vd, 1, result);
            vfpu_eat_prefixes(state);
            return;
        } else if (group == 2 && type == 25) {
            std::uint16_t colors[4]{};
            for (int i = 0; i < 4; ++i) {
                const auto bits = std::bit_cast<std::uint32_t>(source[i]);
                colors[i] = static_cast<std::uint16_t>(
                    (((bits >> 24U) & 0xffU) >> 4U) << 12U |
                    (((bits >> 16U) & 0xffU) >> 4U) << 8U |
                    (((bits >> 8U) & 0xffU) >> 4U) << 4U |
                    ((bits & 0xffU) >> 4U));
            }
            result[0] = std::bit_cast<float>(static_cast<std::uint32_t>(colors[0]) |
                                             static_cast<std::uint32_t>(colors[1]) << 16U);
            result[1] = std::bit_cast<float>(static_cast<std::uint32_t>(colors[2]) |
                                             static_cast<std::uint32_t>(colors[3]) << 16U);
            vfpu_write_vector(state, vd, size == 1 ? 1 : 2, result);
            vfpu_eat_prefixes(state);
            return;
        }
    } else if (op == 0x3c) {
        const auto group = (instruction >> 21U) & 31U;
        if ((instruction & 0xffff0000U) == 0xf3800000U && size >= 2) {
            float matrix[16]{};
            vfpu_read_matrix(state, vs, size, matrix);
            vfpu_write_matrix(state, vd, size, matrix);
            vfpu_eat_prefixes(state);
            return;
        }
        if ((instruction & 0xff808080U) == 0xf2008080U) {
            float matrix[16]{}, scalar[4]{};
            vfpu_read_matrix(state, vs, 4, matrix);
            vfpu_read_vector(state, vt, 1, scalar);
            vfpu_apply_prefix(scalar, 1, state.vfpu_ctrl[1]);
            for (float& value : matrix) {
                value *= scalar[0];
            }
            vfpu_write_matrix(state, vd, 4, matrix);
            vfpu_eat_prefixes(state);
            return;
        }
        if ((instruction & 0xffffff80U) == 0xf3838080U) {
            float matrix[16]{};
            for (int i = 0; i < 4; ++i) {
                matrix[i * 4 + i] = 1.0F;
            }
            vfpu_write_matrix(state, vd, 4, matrix);
            vfpu_eat_prefixes(state);
            return;
        }
        if ((instruction & 0xffffff80U) == 0xf3868080U) {
            float matrix[16]{};
            vfpu_write_matrix(state, vd, 4, matrix);
            vfpu_eat_prefixes(state);
            return;
        }
        if (group <= 3) {
            float left[16]{}, right[16]{}, product[16]{};
            vfpu_read_matrix(state, vs, size, left);
            vfpu_read_matrix(state, vt, size, right);
            for (int row = 0; row < size; ++row) {
                for (int column = 0; column < size; ++column) {
                    for (int i = 0; i < size; ++i) {
                        product[row * 4 + column] +=
                            left[column * 4 + i] * right[row * 4 + i];
                    }
                }
            }
            vfpu_write_matrix(state, vd, size, product);
            vfpu_eat_prefixes(state);
            return;
        }
        if ((instruction & 0xff808080U) == 0xf1008000U) {
            float matrix[16]{}, vector[4]{}, transformed[4]{};
            vfpu_read_matrix(state, vs, 3, matrix);
            vfpu_read_vector(state, vt, 3, vector);
            for (int row = 0; row < 3; ++row) {
                for (int i = 0; i < 3; ++i) {
                    transformed[row] += matrix[row * 4 + i] * vector[i];
                }
            }
            vfpu_write_vector(state, vd, 3, transformed);
            vfpu_eat_prefixes(state);
            return;
        }
        if (group >= 12 && group <= 15) {
            float matrix[16]{}, vector[4]{}, transformed[4]{};
            vfpu_read_matrix(state, vs, 4, matrix);
            vfpu_read_vector(state, vt, 4, vector);
            for (int row = 0; row < 4; ++row) {
                for (int i = 0; i < 4; ++i) {
                    transformed[row] += matrix[row * 4 + i] * vector[i];
                }
            }
            vfpu_write_vector(state, vd, 4, transformed);
            vfpu_eat_prefixes(state);
            return;
        }
        if (group >= 20 && group <= 23) {
            result[0] = source[1] * target[2] - source[2] * target[1];
            result[1] = source[2] * target[0] - source[0] * target[2];
            result[2] = source[0] * target[1] - source[1] * target[0];
        }
    }

    vfpu_apply_destination_prefix(state, result, size);
    vfpu_write_vector(state, vd, size, result);
    vfpu_eat_prefixes(state);
}

} // namespace psprecomp
