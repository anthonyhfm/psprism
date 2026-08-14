#include <refract/psp_sdk_stubs.hpp>
#include <refract/refract.hpp>

#include "host/host.hpp"
#include "ge/ge_command.hpp"
#include "ge/ge_cache.hpp"
#include "ge/ge_draw_packet.hpp"
#include "ge/ge_scheduler.hpp"
#include "ge/ge_state.hpp"
#include "ge/ge_vertex_decoder.hpp"

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

void execute_command_list(
    psprecomp::State& state, std::vector<std::uint8_t>& memory,
    std::uint32_t memory_base, std::uint32_t address,
    std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> commands) {
  auto cursor = address;
  for (const auto [opcode, argument] : commands) {
    store_word(memory, memory_base, cursor, command(opcode, argument));
    cursor += 4U;
  }
  store_word(memory, memory_base, cursor, command(0x0cU, 0U));
  state.gpr[4] = address;
  state.gpr[5] = 0U;
  state.gpr[6] = 0U;
  refract::pspsdk::sceGeListEnQueue(state);
}

int ge_component_tests() {
  CHECK(refract::ge::command_metadata(0x04U).flow ==
        refract::ge::CommandFlow::draw);
  CHECK(refract::ge::command_metadata(0x0aU).name == "CALL");
  CHECK(refract::ge::command_metadata(0xa5U).name == "TEXADDR");
  CHECK(refract::ge::command_metadata(0xc5U).name == "CLUTFORMAT");
  CHECK(refract::ge::command_metadata(0xeaU).flow ==
        refract::ge::CommandFlow::transfer);
  refract::ge::CacheMetricsAccumulator metrics;
  metrics.record_texture(false, 64U);
  metrics.record_texture(true);
  metrics.record_pipeline(false);
  metrics.record_pipeline(true);
  metrics.record_vertex_buffer(false, 128U);
  metrics.record_vertex_buffer(true, 256U);
  const refract::ge::CacheMetrics expected_metrics{
      1U, 1U, 64U, 1U, 1U, 1U, 1U, 384U};
  CHECK(metrics.snapshot() == expected_metrics);
  metrics.reset();
  CHECK(metrics.snapshot() == refract::ge::CacheMetrics{});
  const auto float_xyz = refract::ge::VertexDecoder::layout(0x180U);
  CHECK(float_xyz.position_offset == 0U);
  CHECK(float_xyz.stride == 12U);
  const auto weighted =
      refract::ge::VertexDecoder::layout(0x00800180U | (2U << 9U));
  CHECK(weighted.weight_count == 1U);
  CHECK(weighted.position_offset % 4U == 0U);

  refract::ge::Scheduler scheduler;
  const auto tail = scheduler.enqueue(0x1000U, 0x1010U, 3U, false);
  const auto head = scheduler.enqueue(0x2000U, 0U, 4U, true);
  CHECK(scheduler.status(tail) == refract::ge::ListStatus::queued);
  CHECK(scheduler.draw_status() == refract::ge::ListStatus::queued);
  scheduler.begin(head);
  CHECK(scheduler.status(head) == refract::ge::ListStatus::drawing);
  scheduler.stall(head, 0x2008U, 0x2008U);
  CHECK(scheduler.status(head) == refract::ge::ListStatus::stalled);
  CHECK(scheduler.break_lists(0U) == head);
  CHECK(scheduler.status(head) == refract::ge::ListStatus::paused);
  CHECK(scheduler.continue_lists() == head);
  scheduler.finish(head, 0x2010U, refract::ge::ListState::completed);
  CHECK(scheduler.status(head) == refract::ge::ListStatus::completed);

  refract::ge::Trace trace;
  trace.set_enabled(true);
  trace.record(7U, 0x08801000U, command(0x04U, 3U));
  trace.record(7U, 0x08801004U, command(0x0cU, 0U));
  const auto bytes = trace.serialize();
  refract::ge::Trace decoded;
  CHECK(refract::ge::Trace::deserialize(bytes, decoded));
  CHECK(decoded.records().size() == 2U);
  CHECK(decoded.records()[0] == trace.records()[0]);
  const auto coverage = decoded.command_coverage();
  CHECK(coverage[0x04U] == 1U);
  CHECK(coverage[0x0cU] == 1U);
  const auto replayed_state = decoded.replayed_command_state();
  CHECK(replayed_state[0x04U] == command(0x04U, 3U));
  CHECK(replayed_state[0x0cU] == command(0x0cU, 0U));
  auto corrupt = bytes;
  corrupt[0] ^= 1U;
  CHECK(!refract::ge::Trace::deserialize(corrupt, decoded));

  refract::ge::State source;
  source.commands[0xd5U] = command(0xd5U, 0x12345U);
  source.world_matrix[3] = 42.5F;
  source.vertex_address = 0x08801234U;
  source.address_translation_width = 0x400U;
  std::array<std::uint32_t, refract::ge::context_word_count> context{};
  source.save(context);
  refract::ge::State restored;
  restored.restore(context);
  CHECK(restored.commands == source.commands);
  CHECK(restored.world_matrix == source.world_matrix);
  CHECK(restored.vertex_address == source.vertex_address);
  CHECK(restored.address_translation_width == 0x400U);
  source.commands[0x3bU] = command(0x3bU, 0x654321U);
  CHECK(source.read_command(0x3bU) == command(0x3bU, 0U));
  CHECK(source.read_command(256U) == 0U);

  refract::ge::DrawPacket packet;
  packet.primitive_type = 3U;
  packet.vertices.resize(1U);
  packet.vertices[0].position[0] = 1.0F;
  packet.state.render_target_address = 0x04000000U;
  const auto first_hash = refract::ge::draw_packet_hash(packet);
  CHECK(first_hash == refract::ge::draw_packet_hash(packet));
  packet.state.depth_target_address = 0x04010000U;
  CHECK(first_hash != refract::ge::draw_packet_hash(packet));
  return 0;
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

void shutdown_audio() {}
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
  CHECK(ge_component_tests() == 0);
  constexpr std::uint32_t memory_base = 0x08800000U;
  constexpr std::uint32_t list_address = memory_base + 0x1000U;
  constexpr std::uint32_t vertex_address = memory_base + 0x2000U;
  constexpr std::uint32_t texture_address = memory_base + 0x3000U;
  std::vector<std::uint8_t> memory(0x30000U);
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
    guest_state.gpr[2] = 1U;
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

  constexpr std::uint32_t ringbuffer_address = memory_base + 0x800U;
  constexpr std::uint32_t ringbuffer_data = memory_base + 0xa000U;
  constexpr std::uint32_t ringbuffer_callback = memory_base + 0x6000U;
  constexpr std::uint32_t ringbuffer_gp = 0x08987640U;
  constexpr std::uint32_t mpeg_address = memory_base + 0x9000U;
  constexpr std::uint32_t mpeg_memory = memory_base + 0x10000U;
  state.gpr[4] = ringbuffer_address;
  state.gpr[5] = 2U;
  state.gpr[6] = ringbuffer_data;
  state.gpr[7] = 2U * (2048U + 104U);
  state.gpr[8] = ringbuffer_callback;
  state.gpr[9] = 0x12345678U;
  state.gpr[28] = ringbuffer_gp;
  refract::pspsdk::sceMpegRingbufferConstruct(state);
  CHECK(state.gpr[2] == 0U);
  state.gpr[4] = mpeg_address;
  state.gpr[5] = mpeg_memory;
  state.gpr[6] = 0x10000U;
  state.gpr[7] = ringbuffer_address;
  refract::pspsdk::sceMpegCreate(state);
  CHECK(state.gpr[2] == 0U);
  observed_thread_gp.store(0U);
  observed_thread_pc.store(0U);
  state.gpr[4] = ringbuffer_address;
  state.gpr[5] = 1U;
  state.gpr[6] = 1U;
  state.gpr[28] = 0x08001234U;
  refract::pspsdk::sceMpegRingbufferPut(state);
  CHECK(state.gpr[2] == 1U);
  CHECK(observed_thread_gp.load() == ringbuffer_gp);
  CHECK(observed_thread_pc.load() == ringbuffer_callback);

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
  state.gpr[5] = 1U;
  refract::pspsdk::sceGeListSync(state);
  CHECK(state.gpr[2] == 3U); // PSP_GE_LIST_STALL_REACHED.
  state.gpr[4] = 0x04U;
  refract::pspsdk::sceGeGetCmd(state);
  CHECK(state.gpr[2] == command(0x04U, 1U));

  state.gpr[4] = list_id;
  state.gpr[5] = 0U;
  refract::pspsdk::sceGeListUpdateStallAddr(state);
  CHECK(state.gpr[2] == 0U);
  CHECK(begin_count == 2U);
  CHECK(end_count == 2U);
  CHECK(present_count == 1U);
  CHECK(pending_primitives.empty());
  CHECK(presented_primitives.size() == 2U);
  state.gpr[4] = list_id;
  state.gpr[5] = 1U;
  refract::pspsdk::sceGeListSync(state);
  CHECK(state.gpr[2] == 0U);
  state.gpr[4] = 1U;
  refract::pspsdk::sceGeDrawSync(state);
  CHECK(state.gpr[2] == 0U);

  constexpr std::uint32_t ge_context_address = memory_base + 0x8000U;
  state.gpr[4] = 0x400U;
  refract::pspsdk::sceGeEdramSetAddrTranslation(state);
  CHECK(state.gpr[2] == 0U);
  state.gpr[4] = ge_context_address;
  refract::pspsdk::sceGeSaveContext(state);
  CHECK(state.gpr[2] == 0U);
  state.gpr[4] = 0x800U;
  refract::pspsdk::sceGeEdramSetAddrTranslation(state);
  CHECK(state.gpr[2] == 0x400U);
  state.gpr[4] = ge_context_address;
  refract::pspsdk::sceGeRestoreContext(state);
  CHECK(state.gpr[2] == 0U);
  state.gpr[4] = 0U;
  refract::pspsdk::sceGeEdramSetAddrTranslation(state);
  CHECK(state.gpr[2] == 0x400U);
  state.gpr[4] = 0x300U;
  refract::pspsdk::sceGeEdramSetAddrTranslation(state);
  CHECK(state.gpr[2] != 0U);
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
  CHECK(normal_primitive.state.texture_format == 3U);
  CHECK(normal_primitive.state.texture_buffer_width == 1U);
  CHECK(normal_primitive.state.render_target_stride == 480U);
  CHECK(normal_primitive.state.render_target_format == 0U);
  CHECK(normal_primitive.state.depth_target_address == 0x04000000U);
  CHECK(normal_primitive.state.cull_face);
  CHECK(normal_primitive.state.depth_test);
  CHECK(!normal_primitive.state.depth_write);
  CHECK(normal_primitive.state.alpha_blend);
  CHECK(normal_primitive.state.color_test);
  CHECK(normal_primitive.state.alpha_test);
  CHECK(normal_primitive.state.texture_color_double);
  CHECK(normal_primitive.state.color_write_mask == 0x0fU);
  CHECK(normal_primitive.state.through_coordinates);
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

  constexpr std::uint32_t signed_vertex_address = vertex_address + 0x100U;
  constexpr std::uint32_t signed_list_address = list_address + 0xa00U;
  constexpr std::array<std::uint16_t, 3> signed_vertex{0xfff8U, 16U,
                                                       0xffffU};
  std::memcpy(memory.data() + signed_vertex_address - memory_base,
              signed_vertex.data(), sizeof(signed_vertex));
  const auto signed_through = write_display_list(
      memory, memory_base, signed_list_address, signed_vertex_address,
      texture_address, 0U, 1U);
  store_word(memory, memory_base, signed_list_address + 2U * 4U,
             command(0x12U, 0x00800100U)); // Signed 16-bit XYZ through mode.
  reset_capture();
  enqueue(state, signed_through);
  CHECK(presented_primitives.size() == 1U);
  CHECK(std::abs(presented_primitives[0].first_position[0] -
                 (-8.0F / 240.0F - 1.0F)) < 0.0001F);
  CHECK(std::abs(presented_primitives[0].first_position[1] -
                 (1.0F - 16.0F / 136.0F)) < 0.0001F);
  CHECK(std::abs(presented_primitives[0].first_position[2] - 1.0F) <
        0.0001F);

  constexpr std::uint32_t signed_byte_vertex_address =
      vertex_address + 0x180U;
  constexpr std::uint32_t signed_byte_list_address = list_address + 0xc00U;
  constexpr std::array<std::uint8_t, 3> signed_byte_vertex{0xf8U, 16U,
                                                           0xffU};
  std::memcpy(memory.data() + signed_byte_vertex_address - memory_base,
              signed_byte_vertex.data(), signed_byte_vertex.size());
  const auto signed_byte_through = write_display_list(
      memory, memory_base, signed_byte_list_address,
      signed_byte_vertex_address, texture_address, 0U, 1U);
  store_word(memory, memory_base, signed_byte_list_address + 2U * 4U,
             command(0x12U, 0x00800080U));
  reset_capture();
  enqueue(state, signed_byte_through);
  CHECK(presented_primitives.size() == 1U);
  CHECK(std::abs(presented_primitives[0].first_position[0] -
                 (-8.0F / 240.0F - 1.0F)) < 0.0001F);
  CHECK(std::abs(presented_primitives[0].first_position[1] -
                 (1.0F - 16.0F / 136.0F)) < 0.0001F);
  CHECK(std::abs(presented_primitives[0].first_position[2] -
                 (255.0F / 65535.0F)) < 0.0001F);

  constexpr std::uint32_t clut_address = memory_base + 0x9000U;
  constexpr std::uint32_t clut_setup_address = memory_base + 0x9400U;
  std::array<std::uint8_t, 32> clut{};
  constexpr std::array<std::uint8_t, 4> clut_color_1{0x11U, 0x22U, 0x33U,
                                                    0x44U};
  constexpr std::array<std::uint8_t, 4> clut_color_2{0x55U, 0x66U, 0x77U,
                                                    0x88U};
  std::copy(clut_color_1.begin(), clut_color_1.end(), clut.begin() + 4U);
  std::copy(clut_color_2.begin(), clut_color_2.end(), clut.begin() + 8U);
  std::memcpy(memory.data() + clut_address - memory_base, clut.data(),
              clut.size());
  execute_command_list(
      state, memory, memory_base, clut_setup_address,
      {{0xb0U, clut_address & 0x00fffff0U},
       {0xb1U, (clut_address >> 8U) & 0x000f0000U},
       {0xc5U, 3U | (0xffU << 8U)}, {0xc4U, 1U}});

  constexpr std::uint32_t clut16_list_address = memory_base + 0x9600U;
  const auto clut16 = write_display_list(
      memory, memory_base, clut16_list_address, vertex_address,
      texture_address, 0U, 1U);
  store_word(memory, memory_base, clut16_list_address + 13U * 4U,
             command(0xc3U, 6U));
  constexpr std::uint16_t clut16_index = 1U;
  std::memcpy(memory.data() + texture_address - memory_base, &clut16_index,
              sizeof(clut16_index));
  reset_capture();
  enqueue(state, clut16);
  CHECK(presented_primitives.size() == 1U);
  CHECK(std::equal(clut_color_1.begin(), clut_color_1.end(),
                   presented_primitives[0].texture_pixels.begin()));

  constexpr std::uint32_t clut32_list_address = memory_base + 0x9800U;
  const auto clut32 = write_display_list(
      memory, memory_base, clut32_list_address, vertex_address,
      texture_address, 0U, 1U);
  store_word(memory, memory_base, clut32_list_address + 13U * 4U,
             command(0xc3U, 7U));
  constexpr std::uint32_t clut32_index = 2U;
  std::memcpy(memory.data() + texture_address - memory_base, &clut32_index,
              sizeof(clut32_index));
  reset_capture();
  enqueue(state, clut32);
  CHECK(presented_primitives.size() == 1U);
  CHECK(std::equal(clut_color_2.begin(), clut_color_2.end(),
                   presented_primitives[0].texture_pixels.begin()));

  constexpr std::uint32_t mip_texture_address = memory_base + 0x9a00U;
  constexpr std::uint32_t mip_setup_address = memory_base + 0x9b00U;
  constexpr std::array<std::uint8_t, 4> mip_color{0xdeU, 0xadU, 0xbeU, 0xefU};
  std::memcpy(memory.data() + mip_texture_address - memory_base,
              mip_color.data(), mip_color.size());
  execute_command_list(
      state, memory, memory_base, mip_setup_address,
      {{0xa1U, mip_texture_address & 0x00fffff0U},
       {0xa9U, ((mip_texture_address >> 8U) & 0x000f0000U) | 1U},
       {0xb9U, 0U}, {0xc2U, 1U << 16U},
       {0xc6U, 0x107U}, {0xc8U, 1U | (16U << 16U)}});
  constexpr std::uint32_t mip_list_address = memory_base + 0x9c00U;
  const auto mip = write_display_list(memory, memory_base, mip_list_address,
                                      vertex_address, texture_address, 0U, 1U);
  reset_capture();
  enqueue(state, mip);
  CHECK(presented_primitives.size() == 1U);
  CHECK(presented_primitives[0].state.texture_address == mip_texture_address);
  CHECK(presented_primitives[0].state.texture_mipmap_level == 1U);
  CHECK(presented_primitives[0].state.texture_max_mipmap_level == 1U);
  CHECK(presented_primitives[0].state.texture_lod_bias == 16);
  CHECK(presented_primitives[0].state.texture_min_linear);
  CHECK(presented_primitives[0].state.texture_mag_linear);
  CHECK(presented_primitives[0].state.texture_mipmap_enabled);
  CHECK(presented_primitives[0].state.texture_mipmap_linear);
  CHECK(std::equal(mip_color.begin(), mip_color.end(),
                   presented_primitives[0].texture_pixels.begin()));

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
