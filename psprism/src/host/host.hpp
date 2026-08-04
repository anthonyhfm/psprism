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
};

void initialize_frontend();
void run_event_loop();
void request_frontend_exit();
void present_frame(const std::uint8_t* pixels, std::uint32_t stride,
                   std::uint32_t width, std::uint32_t height,
                   std::uint32_t format);
void begin_ge_frame();
void submit_ge_primitive(std::uint32_t type,
                         std::vector<GeometryVertex> vertices);
ControllerState controller_state();

} // namespace psprism::host
