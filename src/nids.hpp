#pragma once

#include <cstdint>
#include <string_view>

namespace psprecomp {

[[nodiscard]] std::string_view resolve_psp_nid(std::string_view library,
                                               std::uint32_t nid);
[[nodiscard]] std::string_view resolve_psp_nid(std::uint32_t nid);

} // namespace psprecomp
