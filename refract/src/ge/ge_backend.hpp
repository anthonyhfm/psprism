#pragma once

#include "host/host.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace refract::ge {

class RenderBackend {
public:
  virtual ~RenderBackend() = default;
  virtual void begin_frame() = 0;
  virtual void end_frame() = 0;
  virtual void present_frame() = 0;
  virtual void submit(std::uint32_t primitive_type,
                      std::vector<host::GeometryVertex> vertices,
                      std::shared_ptr<const std::vector<std::uint8_t>> texture,
                      std::uint32_t texture_width,
                      std::uint32_t texture_height,
                      host::GeometryState state) = 0;
};

class HostRenderBackend final : public RenderBackend {
public:
  void begin_frame() override { host::begin_ge_frame(); }
  void end_frame() override { host::end_ge_frame(); }
  void present_frame() override { host::present_ge_frame(); }
  void submit(std::uint32_t primitive_type,
              std::vector<host::GeometryVertex> vertices,
              std::shared_ptr<const std::vector<std::uint8_t>> texture,
              std::uint32_t texture_width, std::uint32_t texture_height,
              host::GeometryState state) override {
    host::submit_ge_primitive(primitive_type, std::move(vertices),
                              std::move(texture), texture_width,
                              texture_height, state);
  }
};

} // namespace refract::ge
