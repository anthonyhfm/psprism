#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace psprism::utility {

std::string sfo_string(const std::vector<std::uint8_t>& data,
                       std::string_view wanted_key);
std::vector<std::uint8_t> make_savedata_sfo(std::string_view title,
                                             std::string_view savedata_title,
                                             std::string_view detail);

} // namespace psprism::utility
