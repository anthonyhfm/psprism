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

#include "../nids.hpp"

namespace psprecomp::detail {

std::string import_symbol(const Import& import, const CodeMap* code_map) {
    if (code_map != nullptr) {
        if (const auto* symbol = code_map->symbol_at(import.stub_address)) {
            return *symbol;
        }
    }
    if (const auto symbol = resolve_psp_nid(import.library, import.nid);
        !symbol.empty()) {
        return std::string(symbol);
    }
    return std::string(resolve_psp_nid(import.nid));
}

std::uint32_t word_at(const ExecutableSection& section, std::size_t offset) {
    const auto* bytes = section.bytes.data() + offset;
    return static_cast<std::uint32_t>(bytes[0]) |
           static_cast<std::uint32_t>(bytes[1]) << 8U |
           static_cast<std::uint32_t>(bytes[2]) << 16U |
           static_cast<std::uint32_t>(bytes[3]) << 24U;
}

std::string hex(std::uint32_t value) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::setfill('0') << std::setw(8) << value
           << 'U';
    return stream.str();
}

std::string hex_address(std::uint32_t value) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::setfill('0') << std::setw(8) << value;
    return stream.str();
}

std::string pc_label(std::uint32_t value) {
    std::ostringstream stream;
    stream << "pc_" << std::hex << std::setfill('0') << std::setw(8) << value;
    return stream.str();
}

std::string function_name(std::uint32_t value) {
    std::ostringstream stream;
    stream << "run_function_" << std::hex << std::setfill('0') << std::setw(8)
           << value;
    return stream.str();
}

std::string cpp_string(std::string_view value) {
    std::string result;
    for (const auto character : value) {
        if (character == '\\' || character == '"') {
            result.push_back('\\');
        }
        if (character != '\r' && character != '\n') {
            result.push_back(character);
        }
    }
    return result;
}

std::string make_value(std::string_view value) {
    std::string result;
    for (const auto character : value) {
        if (character == '$') {
            result += "$$";
        } else if (character == '\'') {
            result += "'\\''";
        } else {
            result.push_back(character == '\r' || character == '\n' ||
                                     character == '#'
                                 ? ' '
                                 : character);
        }
    }
    return result;
}

std::string identifier(std::string_view value) {
    std::string result;
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        result.push_back(std::isalnum(byte) || character == '_' ? character
                                                                : '_');
    }
    if (result.empty() || std::isdigit(static_cast<unsigned char>(result[0]))) {
        result.insert(0, "import_");
    }
    return result;
}

bool starts_delayed_branch(std::uint32_t instruction) {
    return decode_allegrex(instruction).has_flag(instruction_delayed_branch);
}

bool needs_post_delay_entry(std::uint32_t instruction) {
    // Compilers place jump-table landing pads directly after unconditional
    // branch delay slots (Duff-style loops are a common example). The target
    // is reached through JR/JALR, so static direct-target discovery cannot see
    // it. Registering pc + 8 for every delayed control transfer is harmless
    // for normal fall-through and makes these indirect entries dispatchable.
    return starts_delayed_branch(instruction);
}

bool may_stop_execution(std::uint32_t instruction) {
    const auto op = instruction >> 26U;
    if (op == 0) {
        const auto function = instruction & 63U;
        return function == 0x0cU || function == 0x0dU;
    }
    return false;
}

} // namespace psprecomp::detail
