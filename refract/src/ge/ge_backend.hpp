#pragma once

#include "ge_draw_packet.hpp"
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
  virtual void submit(DrawPacket packet) = 0;
};

class HostRenderBackend final : public RenderBackend {
public:
  void begin_frame() override { host::begin_ge_frame(); }
  void end_frame() override { host::end_ge_frame(); }
  void present_frame() override { host::present_ge_frame(); }
  void submit(DrawPacket packet) override {
    host::submit_ge_primitive(
        packet.primitive_type, std::move(packet.vertices),
        std::move(packet.texture), packet.texture_width,
        packet.texture_height, packet.state);
  }
};

} // namespace refract::ge
