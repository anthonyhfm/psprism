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

namespace psprecomp {
    std::uint32_t ElfImage::memory_size() const {
        std::uint32_t size = 0;
        for (const auto& segment : load_segments) {
            if (segment.address > std::numeric_limits<std::uint32_t>::max() - segment.memory_size) {
                throw std::runtime_error("load segment address overflow");
            }

            size = std::max(size, segment.address + segment.memory_size);
        }
        return size;
    }

    std::vector<std::uint8_t> ElfImage::memory_image() const {
        std::vector<std::uint8_t> image(memory_size());

        for (const auto& segment : load_segments) {
            if (segment.address > image.size() || segment.bytes.size() > image.size() - segment.address) {
                throw std::runtime_error("load segment is outside memory image");
            }

            std::copy(segment.bytes.begin(), segment.bytes.end(), image.begin() + segment.address);
        }
        return image;
    }

    bool CodeMap::contains(std::uint32_t address) const {
        const auto after = std::upper_bound(excluded_ranges.begin(), excluded_ranges.end(), address,
            [](std::uint32_t value, const AddressRange& range) {
                return value < range.begin;
            }
        );

        if (after == excluded_ranges.begin()) {
            return true;
        }

        const auto& candidate = *std::prev(after);

        return address < candidate.begin || address >= candidate.end;
    }

    const std::string* CodeMap::symbol_at(std::uint32_t address) const {
        const auto found = std::lower_bound(function_symbols.begin(), function_symbols.end(), address,
            [](const FunctionSymbol& symbol, std::uint32_t value) {
                return symbol.address < value;
            }
        );

        return found != function_symbols.end() && found->address == address ? &found->name : nullptr;
    }

    const FunctionRange* CodeMap::function_containing(std::uint32_t address) const {
        const auto after = std::upper_bound(function_ranges.begin(), function_ranges.end(), address,
            [](std::uint32_t value, const FunctionRange& range) {
                return value < range.begin;
            }
        );

        if (after == function_ranges.begin()) {
            return nullptr;
        }

        const auto& candidate = *std::prev(after);

        return address < candidate.end ? &candidate : nullptr;
    }
} // namespace psprecomp
