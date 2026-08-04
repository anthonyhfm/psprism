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

void initialize_frontend();
void run_event_loop();
void request_frontend_exit();
void present_frame(const std::uint8_t* pixels, std::uint32_t stride,
                   std::uint32_t width, std::uint32_t height,
                   std::uint32_t format);
void begin_ge_frame();
void end_ge_frame();
void submit_ge_primitive(
    std::uint32_t type, std::vector<GeometryVertex> vertices,
    std::vector<std::uint8_t> texture = {}, std::uint32_t texture_width = 0,
    std::uint32_t texture_height = 0, bool depth_test = false,
    bool depth_write = false, std::uint32_t depth_function = 1,
    bool alpha_blend = false, bool alpha_test = false,
    std::uint32_t alpha_function = 1, std::uint32_t alpha_reference = 0,
    std::uint32_t alpha_mask = 0xff);
ControllerState controller_state();

} // namespace psprism::host
