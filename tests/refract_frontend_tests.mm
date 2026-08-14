#include "../refract/src/host/macos_frontend.mm"

#include <cstdint>
#include <memory>
#include <vector>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) return __LINE__;                                         \
  } while (false)

namespace refract::host {

std::uint64_t monotonic_microseconds() { return 1000000U; }
std::uint64_t unix_seconds() { return 1700000000U; }
void sleep_microseconds(std::uint32_t) {}
bool submit_audio(const std::int16_t*, std::uint32_t, std::uint32_t, bool,
                  std::uint32_t) {
  return true;
}

} // namespace refract::host

namespace {

void submit_test_primitive(std::uint32_t target, float x) {
  refract::host::GeometryVertex vertex;
  vertex.position[0] = x;
  refract::host::GeometryState state;
  state.render_target_address = target;
  refract::host::submit_ge_primitive(0U, {vertex}, {}, 0U, 0U, state);
}

} // namespace

int main() {
  {
    refract::ge::FramebufferSourceTracker sources;
    constexpr std::uint32_t first_address = 0x04000000U;
    constexpr std::uint32_t second_address = 0x04044000U;
    const std::vector<std::uint8_t> black(16U, 0U);
    auto decoded_video = black;
    decoded_video[3] = 0xffU;

    CHECK(sources.record_cpu_frame(first_address, black));
    CHECK(sources.cpu_is_latest(first_address));
    sources.record_ge_write(first_address);
    CHECK(!sources.cpu_is_latest(first_address));
    CHECK(!sources.record_cpu_frame(first_address, black));
    CHECK(!sources.cpu_is_latest(first_address));
    CHECK(sources.record_cpu_frame(first_address, decoded_video));
    CHECK(sources.cpu_is_latest(first_address));
    CHECK(!sources.cpu_is_latest(second_address));
    sources.reset();
    CHECK(!sources.cpu_is_latest(first_address));
  }

  CHECK(render_target_texture_scale(512U, 512U) == 1.0F);
  CHECK(std::abs(render_target_texture_scale(512U, 272U) -
                 (512.0F / 272.0F)) < 0.0001F);
  CHECK(render_target_texture_scale(0U, 272U) == 1.0F);
  CHECK(std::abs(render_target_texture_scale(256U, 280U) -
                 (256.0F / 280.0F)) < 0.0001F);
  CHECK(render_target_geometry_scale(false, 256U, 512U) == 1.0F);
  CHECK(std::abs(render_target_geometry_scale(true, 256U, 512U) - 0.5F) <
        0.0001F);
  CHECK(refract::host::color_write_mask(0U, 0U) == 0x0fU);
  CHECK(refract::host::color_write_mask(0x00ffffffU, 0xffU) == 0U);
  CHECK(refract::host::color_write_mask(0x0000ffU, 0U) == 0x0eU);
  CHECK(psp_compare_function(6U) == MTLCompareFunctionGreater);
  CHECK(psp_stencil_operation(2U) == MTLStencilOperationReplace);
  CHECK(psp_stencil_operation(4U) == MTLStencilOperationIncrementClamp);

  id<MTLDevice> test_device = MTLCreateSystemDefaultDevice();
  CHECK(test_device != nil);
  MTKView* test_view = [[MTKView alloc] initWithFrame:NSMakeRect(0, 0, 480, 272)
                                               device:test_device];
  test_view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
  test_view.depthStencilPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
  PsprismRenderer* test_renderer =
      [[PsprismRenderer alloc] initWithView:test_view];
  CHECK(test_renderer != nil);
  CHECK(test_renderer.geometryPipeline != nil);
  CHECK(test_renderer.texturedGeometryPipeline != nil);
  refract::host::GeometryState stencil_state;
  stencil_state.depth_test = true;
  stencil_state.depth_function = 5U;
  stencil_state.stencil_test = true;
  stencil_state.stencil_function = 2U;
  stencil_state.stencil_reference = 0x5aU;
  stencil_state.stencil_depth_pass = 2U;
  CHECK([test_renderer depthStencilStateForGeometryState:stencil_state] != nil);

  refract::host::reset_ge_cache_metrics();
  CHECK(refract::host::ge_cache_metrics() == refract::ge::CacheMetrics{});
  ge_cache_counters.record_texture(false, 128U);
  ge_cache_counters.record_pipeline(true);
  ge_cache_counters.record_vertex_buffer(true, 64U);
  const auto cache_metrics = refract::host::ge_cache_metrics();
  CHECK(cache_metrics.texture_misses == 1U);
  CHECK(cache_metrics.texture_upload_bytes == 128U);
  CHECK(cache_metrics.pipeline_hits == 1U);
  CHECK(cache_metrics.vertex_buffer_reuses == 1U);
  CHECK(cache_metrics.vertex_upload_bytes == 64U);
  refract::host::reset_ge_cache_metrics();

  {
    std::lock_guard lock(geometry_mutex);
    building_geometry_batches.clear();
    pending_geometry_batches.clear();
    presented_geometry_batches.clear();
  }

  refract::host::begin_ge_frame();
  submit_test_primitive(0x04000000U, 1.0F);
  refract::host::end_ge_frame();
  CHECK(building_geometry_batches.empty());
  CHECK(pending_geometry_batches.size() == 1U);
  CHECK(presented_geometry_batches.empty());

  refract::host::begin_ge_frame();
  submit_test_primitive(0x04044000U, 2.0F);
  refract::host::end_ge_frame();
  CHECK(pending_geometry_batches.size() == 2U);
  CHECK(pending_geometry_batches[0].state.render_target_address ==
        0x04000000U);
  CHECK(pending_geometry_batches[1].state.render_target_address ==
        0x04044000U);

  refract::host::present_ge_frame();
  CHECK(pending_geometry_batches.empty());
  CHECK(presented_geometry_batches.size() == 2U);
  CHECK(presented_geometry_batches[0].vertices[0].position[0] == 1.0F);
  CHECK(presented_geometry_batches[1].vertices[0].position[0] == 2.0F);

  refract::host::present_ge_frame();
  CHECK(presented_geometry_batches.size() == 2U);

  refract::host::begin_ge_frame();
  submit_test_primitive(0x04000000U, 3.0F);
  refract::host::begin_ge_frame();
  refract::host::end_ge_frame();
  CHECK(building_geometry_batches.empty());
  CHECK(pending_geometry_batches.empty());
  return 0;
}
