#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace refract::ge {

struct VertexLayout {
  std::size_t stride{};
  std::size_t weight_offset{};
  std::size_t texture_offset{};
  std::size_t color_offset{};
  std::size_t normal_offset{};
  std::size_t position_offset{};
  std::uint32_t texture_type{};
  std::uint32_t color_type{};
  std::uint32_t normal_type{};
  std::uint32_t position_type{};
  std::uint32_t weight_type{};
  std::uint32_t weight_count{};
};

constexpr std::size_t component_size(std::uint32_t type) {
  return type == 1U ? 1U : type == 2U ? 2U : type == 3U ? 4U : 0U;
}

constexpr std::size_t align_offset(std::size_t value, std::size_t alignment) {
  return alignment == 0U ? value
                         : (value + alignment - 1U) & ~(alignment - 1U);
}

class VertexDecoder {
public:
  static constexpr VertexLayout layout(std::uint32_t type) {
    VertexLayout result;
    result.texture_type = type & 3U;
    result.color_type = (type >> 2U) & 7U;
    result.normal_type = (type >> 5U) & 3U;
    result.position_type = (type >> 7U) & 3U;
    result.weight_type = (type >> 9U) & 3U;
    result.weight_count = ((type >> 14U) & 7U) + 1U;
    const auto weight_size = component_size(result.weight_type);
    std::size_t offset{};
    std::size_t maximum_alignment{1U};
    if (weight_size != 0U) {
      maximum_alignment = std::max(maximum_alignment, weight_size);
      offset = align_offset(offset, weight_size);
      result.weight_offset = offset;
      offset += weight_size * result.weight_count;
    }
    const auto texture_size = component_size(result.texture_type);
    if (texture_size != 0U) {
      maximum_alignment = std::max(maximum_alignment, texture_size);
      offset = align_offset(offset, texture_size);
      result.texture_offset = offset;
      offset += texture_size * 2U;
    }
    const std::size_t color_size = result.color_type == 7U   ? 4U
                                   : result.color_type >= 4U ? 2U
                                                             : 0U;
    if (color_size != 0U) {
      maximum_alignment = std::max(maximum_alignment, color_size);
      offset = align_offset(offset, color_size);
      result.color_offset = offset;
      offset += color_size;
    }
    const auto normal_size = component_size(result.normal_type);
    if (normal_size != 0U) {
      maximum_alignment = std::max(maximum_alignment, normal_size);
      offset = align_offset(offset, normal_size);
      result.normal_offset = offset;
      offset += normal_size * 3U;
    }
    const auto position_size = component_size(result.position_type);
    maximum_alignment = std::max(maximum_alignment, position_size);
    offset = align_offset(offset, position_size);
    result.position_offset = offset;
    offset += position_size * 3U;
    result.stride = align_offset(offset, maximum_alignment);
    return result;
  }
};

} // namespace refract::ge
