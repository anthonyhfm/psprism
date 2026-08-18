#include "../elf.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace psprecomp
{
    namespace
    {
        std::string_view next_token(std::string_view &rest)
        {
            while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t' ||
                                     rest.front() == '\r' || rest.front() == '\n'))
            {
                rest.remove_prefix(1);
            }
            if (rest.empty())
            {
                return {};
            }
            std::size_t end = 0;
            while (end < rest.size() && rest[end] != ' ' && rest[end] != '\t' &&
                   rest[end] != '\r' && rest[end] != '\n')
            {
                ++end;
            }
            const auto token = rest.substr(0, end);
            rest.remove_prefix(end);
            return token;
        }

        bool parse_u32(std::string_view value, std::uint32_t &out)
        {
            if (value.empty())
            {
                return false;
            }
            int base = 10;
            if (value.size() >= 2U && value[0] == '0' &&
                (value[1] == 'x' || value[1] == 'X'))
            {
                base = 16;
                value.remove_prefix(2U);
                if (value.empty())
                {
                    return false;
                }
            }
            const auto *first = value.data();
            const auto *last = value.data() + value.size();
            const auto [ptr, ec] = std::from_chars(first, last, out, base);
            return ec == std::errc{} && ptr == last;
        }
    } // namespace

    CodeMap load_code_map(const std::filesystem::path &path)
    {
        std::ifstream stream(path);

        if (!stream)
        {
            throw std::runtime_error("cannot open code map: " + path.string());
        }

        CodeMap map;
        std::string line;
        std::size_t line_number = 0;

        while (std::getline(stream, line))
        {
            ++line_number;

            std::string_view view(line);
            while (!view.empty() && (view.front() == ' ' || view.front() == '\t' ||
                                     view.front() == '\r' || view.front() == '\n'))
            {
                view.remove_prefix(1);
            }

            if (view.empty() || view.front() == '#')
            {
                continue;
            }

            const auto kind = next_token(view);
            const auto first = next_token(view);

            const auto parse_address = [&](std::string_view value) -> std::uint32_t
            {
                std::uint32_t parsed = 0;
                if (value.empty() || !parse_u32(value, parsed))
                {
                    throw std::runtime_error("invalid address in code map line " + std::to_string(line_number));
                }

                return parsed;
            };

            if (kind == "entry" && !first.empty())
            {
                map.entry = parse_address(first);
            }
            else if (kind == "version" && !first.empty())
            {
                map.version = parse_address(first);
                if (map.version != 1U && map.version != 2U)
                {
                    throw std::runtime_error("unsupported code map version in line " + std::to_string(line_number));
                }
            }
            else if (kind == "function" && !first.empty())
            {
                const auto address = parse_address(first);
                map.function_starts.push_back(address);
                const auto name = next_token(view);
                if (!name.empty())
                {
                    map.function_symbols.push_back({address, std::string(name)});
                }
            }
            else if (kind == "function_range" && !first.empty())
            {
                const auto second = next_token(view);
                if (second.empty())
                {
                    throw std::runtime_error("invalid code map line " + std::to_string(line_number));
                }

                const auto begin = parse_address(first);
                const auto end = parse_address(second);

                if (begin >= end || (begin & 3U) != 0U || (end & 3U) != 0U)
                {
                    throw std::runtime_error("invalid function range in code map line " + std::to_string(line_number));
                }

                const auto name = next_token(view);
                map.function_ranges.push_back({begin, end, std::string(name)});
                map.function_starts.push_back(begin);

                if (!name.empty())
                {
                    map.function_symbols.push_back({begin, std::string(name)});
                }
            }
            else if (kind == "block" && !first.empty())
            {
                map.block_entries.push_back(parse_address(first));
            }
            else if ((kind == "gp" || kind == "t9") && !first.empty())
            {
                const auto second = next_token(view);
                if (second.empty())
                {
                    throw std::runtime_error("invalid code map line " + std::to_string(line_number));
                }

                RegisterMetadata metadata{parse_address(first), parse_address(second)};
                (kind == "gp" ? map.gp_values : map.t9_values).push_back(metadata);
            }
            else if (kind == "overlay" && !first.empty())
            {
                map.overlay_starts.push_back(parse_address(first));
            }
            else if (kind == "exclude" && !first.empty())
            {
                const auto second = next_token(view);
                if (second.empty())
                {
                    throw std::runtime_error("invalid code map line " + std::to_string(line_number));
                }

                const auto begin = parse_address(first);
                const auto end = parse_address(second);

                if (begin >= end || (begin & 3U) != 0U || (end & 3U) != 0U)
                {
                    throw std::runtime_error("invalid excluded range in code map line " + std::to_string(line_number));
                }

                map.excluded_ranges.push_back({begin, end});
            }
            else
            {
                throw std::runtime_error("invalid code map line " + std::to_string(line_number));
            }
        }

        if (!stream.eof())
        {
            throw std::runtime_error("failed while reading code map: " + path.string());
        }

        std::sort(map.function_starts.begin(), map.function_starts.end());
        map.function_starts.erase(
            std::unique(map.function_starts.begin(), map.function_starts.end()),
            map.function_starts.end());
        std::sort(map.function_ranges.begin(), map.function_ranges.end(),
                  [](const FunctionRange &left, const FunctionRange &right)
                  {
                      return left.begin < right.begin;
                  });
        for (std::size_t i = 1; i < map.function_ranges.size(); ++i)
        {
            if (map.function_ranges[i].begin < map.function_ranges[i - 1U].end)
            {
                throw std::runtime_error("overlapping function ranges in code map");
            }
        }
        std::sort(map.block_entries.begin(), map.block_entries.end());
        map.block_entries.erase(
            std::unique(map.block_entries.begin(), map.block_entries.end()),
            map.block_entries.end());
        std::sort(map.overlay_starts.begin(), map.overlay_starts.end());
        map.overlay_starts.erase(
            std::unique(map.overlay_starts.begin(), map.overlay_starts.end()),
            map.overlay_starts.end());
        std::sort(map.function_symbols.begin(), map.function_symbols.end(),
                  [](const FunctionSymbol &left, const FunctionSymbol &right)
                  {
                      return left.address < right.address;
                  });
        std::sort(map.excluded_ranges.begin(), map.excluded_ranges.end(),
                  [](const AddressRange &left, const AddressRange &right)
                  {
                      return left.begin < right.begin;
                  });
        std::vector<AddressRange> merged_exclusions;
        for (const auto &range : map.excluded_ranges)
        {
            if (!merged_exclusions.empty() &&
                range.begin <= merged_exclusions.back().end)
            {
                merged_exclusions.back().end =
                    std::max(merged_exclusions.back().end, range.end);
            }
            else
            {
                merged_exclusions.push_back(range);
            }
        }
        map.excluded_ranges = std::move(merged_exclusions);
        return map;
    }
} // namespace psprecomp
