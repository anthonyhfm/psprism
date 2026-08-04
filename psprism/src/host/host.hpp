#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace psprism::host {

std::uint64_t monotonic_microseconds();
std::uint64_t unix_seconds();
void sleep_microseconds(std::uint32_t duration);

struct ControllerState {
  std::uint32_t buttons{};
  std::uint8_t analog_x{128};
  std::uint8_t analog_y{128};
};

struct GeometryVertex {
  float position[4]{};
  float color[4]{1.0F, 1.0F, 1.0F, 1.0F};
  float texture[2]{};
};

struct GeometryState {
  std::uint32_t render_target_address{};
  std::uint32_t render_target_width{480};
  std::uint32_t render_target_height{272};
  std::uint32_t texture_address{};
  bool cull_face{};
  bool front_face_clockwise{true};
  bool depth_test{};
  bool depth_write{};
  std::uint32_t depth_function{1};
  bool alpha_blend{};
  std::uint32_t blend_source{};
  std::uint32_t blend_destination{};
  std::uint32_t blend_equation{};
  std::uint32_t blend_fix_a{};
  std::uint32_t blend_fix_b{};
  bool color_test{};
  std::uint32_t color_function{1};
  std::uint32_t color_reference{};
  std::uint32_t color_mask{0x00ffffff};
  bool alpha_test{};
  std::uint32_t alpha_function{1};
  std::uint32_t alpha_reference{};
  std::uint32_t alpha_mask{0xff};
  bool texture_clamp_s{};
  bool texture_clamp_t{};
  bool texture_linear_filter{};
  std::uint32_t texture_function{};
  bool texture_alpha_used{true};
  std::uint32_t texture_environment_color{};
};

void initialize_frontend();
void run_event_loop();
void request_frontend_exit();
void present_frame(const std::uint8_t* pixels, std::uint32_t stride,
                   std::uint32_t width, std::uint32_t height,
                   std::uint32_t format, std::uint32_t address);
void present_ge_frame();
void begin_ge_frame();
void end_ge_frame();
void submit_ge_primitive(std::uint32_t type,
                         std::vector<GeometryVertex> vertices,
                         std::vector<std::uint8_t> texture = {},
                         std::uint32_t texture_width = 0,
                         std::uint32_t texture_height = 0,
                         GeometryState graphics_state = {});
ControllerState controller_state();

} // namespace psprism::host
