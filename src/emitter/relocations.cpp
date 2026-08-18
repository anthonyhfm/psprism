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

    std::vector<std::uint8_t>
    compact_relocations(const std::vector<Relocation> &relocations)
    {
        std::vector<std::uint8_t> result;
        result.reserve(relocations.size() * 2U);
        std::array<std::uint32_t, 256> previous_offsets{};
        Relocation previous;
        bool have_metadata = false;
        for (const auto &relocation : relocations)
        {
            const auto previous_offset = previous_offsets[relocation.patch_segment];
            const auto delta = static_cast<std::int64_t>(relocation.offset) -
                               static_cast<std::int64_t>(previous_offset);
            std::uint8_t type_code = 0xffU;
            switch (relocation.type)
            {
            case 0:
                type_code = 0U;
                break;
            case 2:
                type_code = 1U;
                break;
            case 4:
                type_code = 2U;
                break;
            case 5:
                type_code = 3U;
                break;
            case 6:
                type_code = 4U;
                break;
            default:
                break;
            }
            const bool can_use_short =
                type_code != 0xffU && relocation.patch_segment < 2U &&
                relocation.target_segment < 2U && delta >= 0 &&
                (delta & 3) == 0 && delta <= 28;
            if (can_use_short)
            {
                result.push_back(static_cast<std::uint8_t>(
                    (static_cast<std::uint64_t>(delta) / 4U) << 5U |
                    (relocation.target_segment << 4U) |
                    (relocation.patch_segment << 3U) | type_code));
                previous_offsets[relocation.patch_segment] = relocation.offset;
                previous = relocation;
                have_metadata = true;
                continue;
            }

            std::uint8_t extended_short_code = 0xffU;
            if (relocation.type == 2U)
            {
                extended_short_code = 5U;
            }
            else if (relocation.type == 4U)
            {
                extended_short_code = 6U;
            }
            else if (relocation.type == 6U)
            {
                extended_short_code = 7U;
            }
            const bool can_use_extended_short =
                extended_short_code != 0xffU && relocation.patch_segment == 0U &&
                relocation.target_segment == 0U && delta >= 0 &&
                (delta & 3) == 0 && delta <= 124 &&
                !(extended_short_code == 7U && delta == 124);
            if (can_use_extended_short)
            {
                result.push_back(static_cast<std::uint8_t>(
                    (static_cast<std::uint64_t>(delta) / 4U) << 3U |
                    extended_short_code));
                previous_offsets[relocation.patch_segment] = relocation.offset;
                previous = relocation;
                have_metadata = true;
                continue;
            }

            result.push_back(0xffU);
            const auto zigzag = delta >= 0
                                    ? static_cast<std::uint64_t>(delta) * 2U
                                    : static_cast<std::uint64_t>(-(delta + 1)) *
                                              2U +
                                          1U;
            const bool metadata_changed =
                !have_metadata || relocation.type != previous.type ||
                relocation.patch_segment != previous.patch_segment ||
                relocation.target_segment != previous.target_segment;
            auto tag = (zigzag << 1U) |
                       static_cast<std::uint64_t>(metadata_changed);
            do
            {
                auto byte = static_cast<std::uint8_t>(tag & 0x7fU);
                tag >>= 7U;
                if (tag != 0U)
                {
                    byte |= 0x80U;
                }
                result.push_back(byte);
            } while (tag != 0U);
            if (metadata_changed)
            {
                const bool can_pack = relocation.type < 16U &&
                                      relocation.patch_segment < 4U &&
                                      relocation.target_segment < 4U;
                const auto packed = static_cast<std::uint8_t>(
                    relocation.type | (relocation.patch_segment << 4U) |
                    (relocation.target_segment << 6U));
                if (can_pack && packed != 0xffU)
                {
                    result.push_back(packed);
                }
                else
                {
                    result.push_back(0xffU);
                    result.push_back(relocation.type);
                    result.push_back(relocation.patch_segment);
                    result.push_back(relocation.target_segment);
                }
            }
            previous_offsets[relocation.patch_segment] = relocation.offset;
            previous = relocation;
            have_metadata = true;
        }
        return result;
    }

} // namespace psprecomp::detail
