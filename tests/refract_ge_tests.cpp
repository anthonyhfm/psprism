#include <refract/psp_sdk_stubs.hpp>
#include <refract/refract.hpp>

#include "host/host.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) return __LINE__;                                         \
  } while (false)

namespace {

struct CapturedPrimitive {
  std::uint32_t type{};
  std::size_t vertex_count{};
  std::array<float, 4> first_position{};
  bool has_texture{};
  std::uint32_t texture_width{};
  std::uint32_t texture_height{};
  std::vector<std::uint8_t> texture_pixels;
  refract::host::GeometryState state;
};

std::vector<CapturedPrimitive> building_primitives;
std::vector<CapturedPrimitive> pending_primitives;
std::vector<CapturedPrimitive> presented_primitives;
std::uint32_t begin_count{};
std::uint32_t end_count{};
std::uint32_t present_count{};
std::atomic<std::uint32_t> observed_thread_gp{};
std::atomic<std::uint32_t> observed_thread_pc{};

void reset_capture() {
  building_primitives.clear();
  pending_primitives.clear();
  presented_primitives.clear();
  begin_count = 0U;
  end_count = 0U;
  present_count = 0U;
}

std::uint32_t command(std::uint32_t opcode, std::uint32_t argument) {
  return (opcode << 24U) | (argument & 0x00ffffffU);
}

void store_word(std::vector<std::uint8_t>& memory, std::uint32_t memory_base,
                std::uint32_t address, std::uint32_t value) {
  std::memcpy(memory.data() + address - memory_base, &value, sizeof(value));
}

struct DisplayList {
  std::uint32_t start{};
  std::uint32_t second_primitive{};
};

DisplayList write_display_list(std::vector<std::uint8_t>& memory,
                               std::uint32_t memory_base,
                               std::uint32_t list_address,
                               std::uint32_t vertex_address,
                               std::uint32_t texture_address,
                               std::uint32_t clear_mode,
                               std::uint32_t primitive_count) {
  std::uint32_t cursor = list_address;
  const auto append = [&](std::uint32_t opcode, std::uint32_t argument) {
    store_word(memory, memory_base, cursor, command(opcode, argument));
    cursor += 4U;
  };
  append(0x10U, (vertex_address >> 8U) & 0x000f0000U);
  append(0x01U, vertex_address & 0x00ffffffU);
  append(0x12U, 0x00800180U); // Float XYZ positions in through mode.
  append(0x9dU, 480U);
  append(0x42U, std::bit_cast<std::uint32_t>(240.0F) >> 8U);
  append(0x43U, std::bit_cast<std::uint32_t>(-136.0F) >> 8U);
  append(0x16U, 479U | (271U << 10U));
  append(0xd4U, 0U);
  append(0xd5U, 479U | (271U << 10U));
  append(0x1eU, 1U);
  append(0xa0U, texture_address & 0x00fffff0U);
  append(0xa8U, ((texture_address >> 8U) & 0x000f0000U) | 1U);
  append(0xb8U, 0U); // 1x1 texture.
  append(0xc3U, 3U); // RGBA8888.
  append(0xc9U, 0x00010104U); // Add, texture alpha, color doubling.
  append(0x1dU, 1U); // Cull.
  append(0x21U, 1U); // Blend.
  append(0x22U, 1U); // Alpha test.
  append(0x23U, 1U); // Depth test.
  append(0x27U, 1U); // Color test.
  append(0xe7U, 1U); // Normal rendering depth writes disabled.
  append(0xd3U, clear_mode);
  append(0x04U, 1U);
  const auto second_primitive = cursor;
  if (primitive_count > 1U) append(0x04U, 1U);
  append(0x0cU, 0U);
  return {list_address, second_primitive};
}

void enqueue(psprecomp::State& state, const DisplayList& list,
             std::uint32_t stall_address = 0U) {
  state.gpr[4] = list.start;
  state.gpr[5] = stall_address;
  state.gpr[6] = 0U;
  refract::pspsdk::sceGeListEnQueue(state);
}

} // namespace

namespace refract::host {

std::uint64_t monotonic_microseconds() { return 1000000U; }
std::uint64_t unix_seconds() { return 1700000000U; }
void sleep_microseconds(std::uint32_t) {}
bool submit_audio(const std::int16_t*, std::uint32_t, std::uint32_t, bool,
                  std::uint32_t) {
  return true;
}
void initialize_frontend() {}
void set_verbose_logging(bool) {}
void run_event_loop() {}
void request_frontend_exit() {}
void present_frame(const std::uint8_t*, std::uint32_t, std::uint32_t,
                   std::uint32_t, std::uint32_t, std::uint32_t) {}

void begin_ge_frame() {
  ++begin_count;
  building_primitives.clear();
}

void end_ge_frame() {
  ++end_count;
  pending_primitives.insert(
      pending_primitives.end(),
      std::make_move_iterator(building_primitives.begin()),
      std::make_move_iterator(building_primitives.end()));
  building_primitives.clear();
}

void present_ge_frame() {
  ++present_count;
  presented_primitives.insert(
      presented_primitives.end(),
      std::make_move_iterator(pending_primitives.begin()),
      std::make_move_iterator(pending_primitives.end()));
  pending_primitives.clear();
}

void submit_ge_primitive(std::uint32_t type,
                         std::vector<GeometryVertex> vertices,
                         std::shared_ptr<const std::vector<std::uint8_t>> texture,
                         std::uint32_t texture_width,
                         std::uint32_t texture_height,
                         GeometryState state) {
  std::array<float, 4> first_position{};
  if (!vertices.empty())
    std::copy(std::begin(vertices.front().position),
              std::end(vertices.front().position), first_position.begin());
  building_primitives.push_back({type, vertices.size(), first_position,
                                 texture != nullptr,
                                 texture_width, texture_height,
                                 texture != nullptr ? *texture
                                                    : std::vector<std::uint8_t>{},
                                 state});
}

ControllerState controller_state() { return {}; }
void present_dialog(DialogModel) {}
std::optional<DialogResult> poll_dialog_result(std::uint64_t) {
  return std::nullopt;
}
void dismiss_dialog(std::uint64_t) {}
bool dialog_visible() { return false; }

} // namespace refract::host

int main() {
  constexpr std::uint32_t memory_base = 0x08800000U;
  constexpr std::uint32_t list_address = memory_base + 0x1000U;
  constexpr std::uint32_t vertex_address = memory_base + 0x2000U;
  constexpr std::uint32_t texture_address = memory_base + 0x3000U;
  std::vector<std::uint8_t> memory(0x10000U);
  const std::array<float, 6> vertices{10.0F, 20.0F, 0.5F,
                                      30.0F, 40.0F, 0.5F};
  std::memcpy(memory.data() + vertex_address - memory_base, vertices.data(),
              sizeof(vertices));
  constexpr std::array<std::uint8_t, 4> texture{0x20U, 0x40U, 0x80U, 0xffU};
  std::memcpy(memory.data() + texture_address - memory_base, texture.data(),
              texture.size());

  refract::Configuration configuration;
  configuration.disc_root = std::filesystem::temp_directory_path();
  configuration.writable_root = std::filesystem::temp_directory_path();
  configuration.guest_executor = [](psprecomp::State& guest_state) {
    observed_thread_gp.store(guest_state.gpr[28]);
    observed_thread_pc.store(guest_state.pc);
    guest_state.stop_reason = psprecomp::StopReason::returned;
  };
  refract::Runtime::instance().configure(memory.data(), memory.size(),
                                         memory_base,
                                         std::move(configuration));
  psprecomp::State state{};
  state.memory = memory.data();
  state.memory_size = memory.size();
  state.memory_base = memory_base;
  refract::Runtime::instance().prepare_state(state);

  constexpr std::uint32_t event_name_address = memory_base + 0x5000U;
  constexpr std::uint32_t event_status_address = memory_base + 0x5100U;
  constexpr std::uint32_t event_output_address = memory_base + 0x5200U;
  constexpr char event_name[] = "mpeg-ready";
  std::memcpy(memory.data() + event_name_address - memory_base, event_name,
              sizeof(event_name));
  state.gpr[4] = event_name_address;
  state.gpr[5] = 0x200U;
  state.gpr[6] = 1U;
  state.gpr[7] = 0U;
  refract::pspsdk::sceKernelCreateEventFlag(state);
  const auto event_id = state.gpr[2];
  CHECK(event_id != 0U);

  store_word(memory, memory_base, event_status_address, 52U);
  state.gpr[4] = event_id;
  state.gpr[5] = event_status_address;
  refract::pspsdk::sceKernelReferEventFlagStatus(state);
  CHECK(state.gpr[2] == 0U);
  const auto* event_status =
      memory.data() + event_status_address - memory_base;
  std::uint32_t status_size{};
  std::uint32_t status_attributes{};
  std::uint32_t status_initial_bits{};
  std::uint32_t status_current_bits{};
  std::memcpy(&status_size, event_status, sizeof(status_size));
  std::memcpy(&status_attributes, event_status + 36U,
              sizeof(status_attributes));
  std::memcpy(&status_initial_bits, event_status + 40U,
              sizeof(status_initial_bits));
  std::memcpy(&status_current_bits, event_status + 44U,
              sizeof(status_current_bits));
  CHECK(status_size == 52U);
  CHECK(std::strcmp(reinterpret_cast<const char*>(event_status + 4U),
                    event_name) == 0);
  CHECK(status_attributes == 0x200U);
  CHECK(status_initial_bits == 1U);
  CHECK(status_current_bits == 1U);

  state.gpr[4] = event_id;
  state.gpr[5] = 2U;
  refract::pspsdk::sceKernelSetEventFlag(state);
  CHECK(state.gpr[2] == 0U);
  state.gpr[4] = event_id;
  state.gpr[5] = 3U;
  state.gpr[6] = 0x20U;
  state.gpr[7] = event_output_address;
  refract::pspsdk::sceKernelPollEventFlag(state);
  CHECK(state.gpr[2] == 0U);
  std::uint32_t observed_event_bits{};
  std::memcpy(&observed_event_bits,
              memory.data() + event_output_address - memory_base,
              sizeof(observed_event_bits));
  CHECK(observed_event_bits == 3U);
  store_word(memory, memory_base, event_status_address, 52U);
  state.gpr[4] = event_id;
  state.gpr[5] = event_status_address;
  refract::pspsdk::sceKernelReferEventFlagStatus(state);
  std::memcpy(&status_current_bits, event_status + 44U,
              sizeof(status_current_bits));
  CHECK(status_current_bits == 0U);

  reset_capture();
  const auto streamed = write_display_list(
      memory, memory_base, list_address, vertex_address, texture_address,
      0x701U, 2U);
  enqueue(state, streamed, streamed.second_primitive);
  const auto list_id = state.gpr[2];
  CHECK(list_id != 0U);
  CHECK(begin_count == 1U);
  CHECK(end_count == 1U);
  CHECK(present_count == 0U);
  CHECK(pending_primitives.size() == 1U);
  CHECK(presented_primitives.empty());

  state.gpr[4] = list_id;
  state.gpr[5] = 0U;
  refract::pspsdk::sceGeListUpdateStallAddr(state);
  CHECK(state.gpr[2] == 0U);
  CHECK(begin_count == 2U);
  CHECK(end_count == 2U);
  CHECK(present_count == 1U);
  CHECK(pending_primitives.empty());
  CHECK(presented_primitives.size() == 2U);
  for (const auto& primitive : presented_primitives) {
    CHECK(primitive.vertex_count == 1U);
    CHECK(!primitive.has_texture);
    CHECK(primitive.state.texture_address == 0U);
    CHECK(!primitive.state.cull_face);
    CHECK(!primitive.state.depth_test);
    CHECK(primitive.state.depth_write);
    CHECK(!primitive.state.alpha_blend);
    CHECK(!primitive.state.color_test);
    CHECK(!primitive.state.alpha_test);
    CHECK(primitive.state.color_write_mask == 0x0fU);
  }

  reset_capture();
  const auto alpha_depth_clear = write_display_list(
      memory, memory_base, list_address + 0x200U, vertex_address,
      texture_address, 0x601U, 1U);
  enqueue(state, alpha_depth_clear);
  CHECK(presented_primitives.size() == 1U);
  CHECK(presented_primitives[0].state.color_write_mask == 0x08U);
  CHECK(presented_primitives[0].state.depth_write);

  reset_capture();
  const auto color_depth_clear = write_display_list(
      memory, memory_base, list_address + 0x400U, vertex_address,
      texture_address, 0x501U, 1U);
  enqueue(state, color_depth_clear);
  CHECK(presented_primitives.size() == 1U);
  CHECK(presented_primitives[0].state.color_write_mask == 0x07U);
  CHECK(presented_primitives[0].state.depth_write);

  reset_capture();
  const auto normal = write_display_list(
      memory, memory_base, list_address + 0x600U, vertex_address,
      texture_address, 0U, 1U);
  enqueue(state, normal);
  CHECK(presented_primitives.size() == 1U);
  const auto& normal_primitive = presented_primitives[0];
  CHECK(normal_primitive.has_texture);
  CHECK(normal_primitive.texture_width == 1U);
  CHECK(normal_primitive.texture_height == 1U);
  CHECK(normal_primitive.state.texture_address == texture_address);
  CHECK(normal_primitive.state.cull_face);
  CHECK(normal_primitive.state.depth_test);
  CHECK(!normal_primitive.state.depth_write);
  CHECK(normal_primitive.state.alpha_blend);
  CHECK(normal_primitive.state.color_test);
  CHECK(normal_primitive.state.alpha_test);
  CHECK(normal_primitive.state.texture_color_double);
  CHECK(normal_primitive.state.color_write_mask == 0x0fU);
  const auto normal_first_position = normal_primitive.first_position;

  constexpr std::uint32_t clipped_list_address = list_address + 0x700U;
  const auto clipped = write_display_list(
      memory, memory_base, clipped_list_address, vertex_address,
      texture_address, 0U, 1U);
  // Narrow the scissor without changing the framebuffer viewport.  This is
  // how Tetris clips placed pieces; it must not change their scale.
  store_word(memory, memory_base, clipped_list_address + 8U * 4U,
             command(0xd5U, 239U | (135U << 10U)));
  reset_capture();
  enqueue(state, clipped);
  CHECK(presented_primitives.size() == 1U);
  const auto& clipped_primitive = presented_primitives[0];
  CHECK(clipped_primitive.state.render_target_width == 480U);
  CHECK(clipped_primitive.state.render_target_height == 272U);
  CHECK(clipped_primitive.state.scissor_right == 240U);
  CHECK(clipped_primitive.state.scissor_bottom == 136U);
  CHECK(std::abs(clipped_primitive.first_position[0] -
                 normal_first_position[0]) < 0.0001F);
  CHECK(std::abs(clipped_primitive.first_position[1] -
                 normal_first_position[1]) < 0.0001F);

  constexpr std::uint32_t dxt_list_address = list_address + 0x800U;
  const auto dxt = write_display_list(
      memory, memory_base, dxt_list_address, vertex_address, texture_address,
      0U, 1U);
  store_word(memory, memory_base, dxt_list_address + 11U * 4U,
             command(0xa8U,
                     ((texture_address >> 8U) & 0x000f0000U) | 4U));
  store_word(memory, memory_base, dxt_list_address + 12U * 4U,
             command(0xb8U, 2U | (2U << 8U)));
  store_word(memory, memory_base, dxt_list_address + 13U * 4U,
             command(0xc3U, 8U));
  constexpr std::array<std::uint8_t, 8> red_dxt1_block{
      0U, 0U, 0U, 0U, 0x00U, 0xf8U, 0x00U, 0x00U};
  std::memcpy(memory.data() + texture_address - memory_base,
              red_dxt1_block.data(), red_dxt1_block.size());
  reset_capture();
  enqueue(state, dxt);
  CHECK(presented_primitives.size() == 1U);
  CHECK(presented_primitives[0].texture_width == 4U);
  CHECK(presented_primitives[0].texture_height == 4U);
  CHECK(presented_primitives[0].texture_pixels.size() == 4U * 4U * 4U);
  CHECK(presented_primitives[0].texture_pixels[0] == 0xf8U);
  CHECK(presented_primitives[0].texture_pixels[1] == 0U);
  CHECK(presented_primitives[0].texture_pixels[2] == 0U);
  CHECK(presented_primitives[0].texture_pixels[3] == 0xffU);

  constexpr std::uint32_t thread_name_address = memory_base + 0x700U;
  constexpr char thread_name[] = "gp-regression";
  std::memcpy(memory.data() + thread_name_address - memory_base, thread_name,
              sizeof(thread_name));
  constexpr std::uint32_t thread_entry = memory_base + 0x5000U;
  constexpr std::uint32_t guest_gp = 0x089abc00U;
  state.gpr[4] = thread_name_address;
  state.gpr[5] = thread_entry;
  state.gpr[6] = 32U;
  state.gpr[7] = 0x4000U;
  state.gpr[8] = 0U;
  refract::pspsdk::sceKernelCreateThread(state);
  const auto thread_id = state.gpr[2];
  CHECK(thread_id != 0U);
  state.gpr[4] = thread_id;
  state.gpr[5] = 0U;
  state.gpr[6] = 0U;
  state.gpr[28] = guest_gp;
  refract::pspsdk::sceKernelStartThread(state);
  CHECK(state.gpr[2] == 0U);
  refract::Runtime::instance().wait_for_guest_threads();
  CHECK(observed_thread_gp.load() == guest_gp);
  CHECK(observed_thread_pc.load() == thread_entry);
  return 0;
}
