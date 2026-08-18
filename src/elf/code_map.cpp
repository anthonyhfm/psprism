#include "../elf.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace psprecomp
{
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

            if (line.empty() || line[0] == '#')
            {
                continue;
            }

            std::istringstream fields(line);
            std::string kind;
            std::string first;
            std::string second;
            fields >> kind >> first;

            const auto parse_address = [&](const std::string &value)
            {
                std::size_t consumed = 0;
                const auto parsed = std::stoul(value, &consumed, 0);

                if (consumed != value.size() ||
                    parsed > std::numeric_limits<std::uint32_t>::max())
                {
                    throw std::runtime_error("invalid address in code map line " + std::to_string(line_number));
                }

                return static_cast<std::uint32_t>(parsed);
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
                std::string name;
                if (fields >> name)
                {
                    map.function_symbols.push_back({address, std::move(name)});
                }
            }
            else if (kind == "function_range" && !first.empty() && fields >> second)
            {
                const auto begin = parse_address(first);
                const auto end = parse_address(second);

                if (begin >= end || (begin & 3U) != 0U || (end & 3U) != 0U)
                {
                    throw std::runtime_error("invalid function range in code map line " + std::to_string(line_number));
                }

                std::string name;
                fields >> name;
                map.function_ranges.push_back({begin, end, name});
                map.function_starts.push_back(begin);

                if (!name.empty())
                {
                    map.function_symbols.push_back({begin, std::move(name)});
                }
            }
            else if (kind == "block" && !first.empty())
            {
                map.block_entries.push_back(parse_address(first));
            }
            else if ((kind == "gp" || kind == "t9") && !first.empty() && fields >> second)
            {
                RegisterMetadata metadata{parse_address(first), parse_address(second)};
                (kind == "gp" ? map.gp_values : map.t9_values).push_back(metadata);
            }
            else if (kind == "overlay" && !first.empty())
            {
                map.overlay_starts.push_back(parse_address(first));
            }
            else if (kind == "exclude" && !first.empty() && fields >> second)
            {
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
