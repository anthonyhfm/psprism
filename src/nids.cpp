#include "nids.hpp"

#include <array>

namespace psprecomp {
namespace {

struct NidEntry {
    std::string_view library;
    std::uint32_t nid;
    std::string_view symbol;
};

constexpr auto entries = std::to_array<NidEntry>({
#include "psp_nids.inc"
});

} // namespace

std::string_view resolve_psp_nid(std::string_view library, std::uint32_t nid) {
    for (const auto& entry : entries) {
        if (entry.nid == nid && entry.library == library) {
            return entry.symbol;
        }
    }
    return {};
}

std::string_view resolve_psp_nid(std::uint32_t nid) {
    std::string_view symbol{};
    for (const auto& entry : entries) {
        if (entry.nid == nid) {
            if (symbol.empty()) {
                symbol = entry.symbol;
            } else if (symbol != entry.symbol) {
                return {};
            }
        }
    }
    return symbol;
}

} // namespace psprecomp
