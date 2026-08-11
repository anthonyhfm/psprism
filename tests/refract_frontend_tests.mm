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
