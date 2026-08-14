#pragma once

#include "host/host.hpp"

#include <bit>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace refract::ge {

inline std::uint64_t content_hash(std::span<const std::uint8_t> bytes) {
  std::uint64_t hash = 0xcbf29ce484222325ULL;
  for (const auto byte : bytes) {
    hash ^= byte;
    hash *= 0x100000001b3ULL;
  }
  return hash;
}

struct DrawPacket {
  std::uint32_t primitive_type{};
  std::vector<host::GeometryVertex> vertices;
  std::shared_ptr<const std::vector<std::uint8_t>> texture;
  std::uint32_t texture_width{};
  std::uint32_t texture_height{};
  host::GeometryState state;
};

// Stable hash for trace/replay goldens.  This deliberately hashes the packet's
// PSP-facing data instead of host object identities.
inline std::uint64_t draw_packet_hash(const DrawPacket& packet) {
  std::uint64_t hash = 0xcbf29ce484222325ULL;
  const auto add_bytes = [&](std::span<const std::uint8_t> bytes) {
    for (const auto byte : bytes) {
      hash ^= byte;
      hash *= 0x100000001b3ULL;
    }
  };
  const auto add_u32 = [&](std::uint32_t value) {
    const std::uint8_t bytes[]{static_cast<std::uint8_t>(value),
                               static_cast<std::uint8_t>(value >> 8U),
                               static_cast<std::uint8_t>(value >> 16U),
                               static_cast<std::uint8_t>(value >> 24U)};
    add_bytes(bytes);
  };
  add_u32(packet.primitive_type);
  add_u32(static_cast<std::uint32_t>(packet.vertices.size()));
  for (const auto& vertex : packet.vertices) {
    for (const auto value : vertex.position) add_u32(std::bit_cast<std::uint32_t>(value));
    for (const auto value : vertex.color) add_u32(std::bit_cast<std::uint32_t>(value));
    for (const auto value : vertex.texture) add_u32(std::bit_cast<std::uint32_t>(value));
  }
  add_u32(packet.texture_width);
  add_u32(packet.texture_height);
  if (packet.texture != nullptr) add_bytes(*packet.texture);
  const auto& state = packet.state;
  add_u32(state.render_target_address);
  add_u32(state.render_target_stride);
  add_u32(state.render_target_format);
  add_u32(state.render_target_width);
  add_u32(state.render_target_height);
  add_u32(state.depth_target_address);
  add_u32(state.depth_target_stride);
  add_u32(state.scissor_left);
  add_u32(state.scissor_top);
  add_u32(state.scissor_right);
  add_u32(state.scissor_bottom);
  add_u32(state.texture_address);
  add_u32(state.texture_format);
  add_u32(state.texture_buffer_width);
  add_u32(state.texture_mipmap_level);
  add_u32(state.texture_max_mipmap_level);
  add_u32(static_cast<std::uint32_t>(state.texture_lod_bias));
  add_u32(static_cast<std::uint32_t>(state.texture_generation));
  add_u32(static_cast<std::uint32_t>(state.texture_generation >> 32U));
  add_u32(static_cast<std::uint32_t>(state.texture_content_hash));
  add_u32(static_cast<std::uint32_t>(state.texture_content_hash >> 32U));
  add_u32(state.through_coordinates);
  add_u32(state.cull_face);
  add_u32(state.front_face_clockwise);
  add_u32(state.depth_test);
  add_u32(state.depth_write);
  add_u32(state.depth_function);
  add_u32(state.stencil_test);
  add_u32(state.stencil_function);
  add_u32(state.stencil_reference);
  add_u32(state.stencil_read_mask);
  add_u32(state.stencil_write_mask);
  add_u32(state.stencil_fail);
  add_u32(state.stencil_depth_fail);
  add_u32(state.stencil_depth_pass);
  add_u32(state.clear_stencil);
  add_u32(state.alpha_blend);
  add_u32(state.color_write_mask);
  add_u32(state.blend_source);
  add_u32(state.blend_destination);
  add_u32(state.blend_equation);
  add_u32(state.blend_fix_a);
  add_u32(state.blend_fix_b);
  add_u32(state.color_test);
  add_u32(state.color_function);
  add_u32(state.color_reference);
  add_u32(state.color_mask);
  add_u32(state.alpha_test);
  add_u32(state.alpha_function);
  add_u32(state.alpha_reference);
  add_u32(state.alpha_mask);
  add_u32(state.texture_clamp_s);
  add_u32(state.texture_clamp_t);
  add_u32(state.texture_linear_filter);
  add_u32(state.texture_min_linear);
  add_u32(state.texture_mag_linear);
  add_u32(state.texture_mipmap_enabled);
  add_u32(state.texture_mipmap_linear);
  add_u32(state.texture_function);
  add_u32(state.texture_alpha_used);
  add_u32(state.texture_color_double);
  add_u32(state.texture_environment_color);
  return hash;
}

} // namespace refract::ge
