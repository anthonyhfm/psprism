#pragma once

#include "ge_trace.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>

namespace refract::ge {

inline constexpr std::size_t context_word_count = 512U;

struct State {
  std::mutex mutex;
  std::array<std::uint32_t, 256> commands{};
  std::uint32_t vertex_address{};
  std::uint32_t index_address{};
  std::uint32_t offset_address{};
  std::array<float, 12> world_matrix{};
  std::array<float, 12> view_matrix{};
  std::array<float, 16> projection_matrix{};
  std::array<float, 96> bone_matrices{};
  std::array<std::uint8_t, 1024> clut{};
  std::uint32_t world_matrix_index{};
  std::uint32_t view_matrix_index{};
  std::uint32_t projection_matrix_index{};
  std::uint32_t bone_matrix_index{};
  std::uint32_t address_translation_width{};
  Trace trace;

  void reset() {
    commands.fill(0U);
    vertex_address = 0U;
    index_address = 0U;
    offset_address = 0U;
    world_matrix.fill(0.0F);
    view_matrix.fill(0.0F);
    projection_matrix.fill(0.0F);
    bone_matrices.fill(0.0F);
    clut.fill(0U);
    world_matrix_index = 0U;
    view_matrix_index = 0U;
    projection_matrix_index = 0U;
    bone_matrix_index = 0U;
    address_translation_width = 0U;
    trace.clear();
  }

  std::uint32_t read_command(std::uint32_t command) const {
    if (command >= commands.size()) return 0U;
    auto value = commands[command];
    switch (command) {
    case 0x2bU:
    case 0x3bU:
    case 0x3dU:
    case 0x3fU: value &= 0xff000000U; break;
    case 0x2aU: value &= 0xff00007fU; break;
    case 0x3aU:
    case 0x3cU:
    case 0x3eU: value &= 0xff00000fU; break;
    default: break;
    }
    return value;
  }

  void save(std::span<std::uint32_t, context_word_count> output) const {
    std::fill(output.begin(), output.end(), 0U);
    std::copy(commands.begin(), commands.end(), output.begin());
    auto cursor = output.begin() + commands.size();
    auto store_floats = [&](const auto& values) {
      for (const auto value : values) *cursor++ = std::bit_cast<std::uint32_t>(value);
    };
    store_floats(world_matrix);
    store_floats(view_matrix);
    store_floats(projection_matrix);
    store_floats(bone_matrices);
    *cursor++ = vertex_address;
    *cursor++ = index_address;
    *cursor++ = offset_address;
    *cursor++ = world_matrix_index;
    *cursor++ = view_matrix_index;
    *cursor++ = projection_matrix_index;
    *cursor++ = bone_matrix_index;
    *cursor = address_translation_width;
  }

  void restore(std::span<const std::uint32_t, context_word_count> input) {
    std::copy_n(input.begin(), commands.size(), commands.begin());
    auto cursor = input.begin() + commands.size();
    auto load_floats = [&](auto& values) {
      for (auto& value : values) value = std::bit_cast<float>(*cursor++);
    };
    load_floats(world_matrix);
    load_floats(view_matrix);
    load_floats(projection_matrix);
    load_floats(bone_matrices);
    vertex_address = *cursor++;
    index_address = *cursor++;
    offset_address = *cursor++;
    world_matrix_index = *cursor++;
    view_matrix_index = *cursor++;
    projection_matrix_index = *cursor++;
    bone_matrix_index = *cursor++;
    address_translation_width = *cursor;
  }
};

} // namespace refract::ge
