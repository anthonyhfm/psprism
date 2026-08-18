#include <refract/psp_sdk_stubs.hpp>
#include <refract/refract.hpp>

#include "host/host.hpp"
#include "stubs/mpeg/mpeg_state.hpp"
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
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <thread>
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
  std::array<float, 3> first_texture{};
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
std::atomic<bool> sleeping_thread_entered{};
std::atomic<bool> sleeping_thread_resumed{};
std::atomic<std::uint32_t> alarm_callback_count{};
std::atomic<std::uint32_t> alarm_callback_argument{};
std::atomic<std::uint32_t> vblank_callback_count{};
std::atomic<std::uint32_t> vblank_callback_argument{};

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
  source.texture_matrix[7] = 12.25F;
  source.vertex_address = 0x08801234U;
  source.address_translation_width = 0x400U;
  std::array<std::uint32_t, refract::ge::context_word_count> context{};
  source.save(context);
  refract::ge::State restored;
  restored.restore(context);
  CHECK(restored.commands == source.commands);
  CHECK(restored.world_matrix == source.world_matrix);
  CHECK(restored.texture_matrix == source.texture_matrix);
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
  std::array<float, 3> first_texture{};
  if (!vertices.empty()) {
    std::copy(std::begin(vertices.front().position),
              std::end(vertices.front().position), first_position.begin());
    std::copy(std::begin(vertices.front().texture),
              std::end(vertices.front().texture), first_texture.begin());
  }
  building_primitives.push_back({type, vertices.size(), first_position,
                                 first_texture,
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

  constexpr std::uint32_t sliced_execution_address = memory_base + 0x7000U;
  constexpr std::uint32_t sleeping_thread_entry = memory_base + 0x7100U;
  constexpr std::uint32_t alarm_callback_entry = memory_base + 0x7180U;
  constexpr std::uint32_t vblank_callback_entry = memory_base + 0x71c0U;
  std::atomic<std::uint32_t> execution_slices{};
  refract::Configuration configuration;
  configuration.disc_root = std::filesystem::temp_directory_path();
  configuration.writable_root = std::filesystem::temp_directory_path();
  configuration.guest_executor = [&](psprecomp::State& guest_state) {
    if (guest_state.pc == sliced_execution_address) {
      guest_state.stop_reason = ++execution_slices < 3U
                                    ? psprecomp::StopReason::step_limit
                                    : psprecomp::StopReason::returned;
      return;
    }
    if (guest_state.pc == sleeping_thread_entry) {
      sleeping_thread_entered.store(true);
      sleeping_thread_entered.notify_one();
      refract::pspsdk::sceKernelSleepThread(guest_state);
      sleeping_thread_resumed.store(true);
      guest_state.stop_reason = psprecomp::StopReason::returned;
      return;
    }
    if (guest_state.pc == alarm_callback_entry) {
      alarm_callback_argument.store(guest_state.gpr[4]);
      ++alarm_callback_count;
      guest_state.gpr[2] = 0U;
      guest_state.stop_reason = psprecomp::StopReason::returned;
      return;
    }
    if (guest_state.pc == vblank_callback_entry) {
      vblank_callback_argument.store(guest_state.gpr[5]);
      ++vblank_callback_count;
      guest_state.gpr[2] = 0U;
      guest_state.stop_reason = psprecomp::StopReason::returned;
      return;
    }
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

  constexpr std::uint32_t memory_stick_device = memory_base + 0x7500U;
  constexpr std::uint32_t memory_stick_status = memory_base + 0x7540U;
  constexpr std::uint32_t stack_decoy_status = memory_base + 0x7550U;
  constexpr std::uint32_t devctl_stack = memory_base + 0x7800U;
  constexpr char memory_stick_controller[] = "mscmhc0:";
  std::memcpy(memory.data() + memory_stick_device - memory_base,
              memory_stick_controller, sizeof(memory_stick_controller));
  psprecomp::store32(state, devctl_stack + 16U, stack_decoy_status);
  psprecomp::store32(state, devctl_stack + 20U, 4U);
  state.gpr[4] = memory_stick_device;
  state.gpr[5] = 0x02025806U;
  state.gpr[6] = 0U;
  state.gpr[7] = 0U;
  state.gpr[8] = memory_stick_status;
  state.gpr[9] = 4U;
  state.gpr[29] = devctl_stack;
  refract::pspsdk::sceIoDevctl(state);
  CHECK(state.gpr[2] == 0U);
  CHECK(psprecomp::load32(state, memory_stick_status) == 1U);
  CHECK(psprecomp::load32(state, stack_decoy_status) == 0U);
  state.gpr[29] = 0U;

  auto sliced_state = state;
  sliced_state.pc = sliced_execution_address;
  refract::Runtime::instance().execute_guest(sliced_state);
  CHECK(sliced_state.stop_reason == psprecomp::StopReason::returned);
  CHECK(execution_slices.load() == 3U);

  constexpr std::uint32_t sleeper_name_address = memory_base + 0x7200U;
  constexpr char sleeper_name[] = "sleep-regression";
  std::memcpy(memory.data() + sleeper_name_address - memory_base, sleeper_name,
              sizeof(sleeper_name));
  state.gpr[4] = sleeper_name_address;
  state.gpr[5] = sleeping_thread_entry;
  state.gpr[6] = 32U;
  state.gpr[7] = 0x4000U;
  state.gpr[8] = 0U;
  refract::pspsdk::sceKernelCreateThread(state);
  const auto sleeping_thread_id = state.gpr[2];
  CHECK(sleeping_thread_id >= 0x100U);
  state.gpr[4] = sleeping_thread_id;
  state.gpr[5] = 0U;
  state.gpr[6] = 0U;
  refract::pspsdk::sceKernelStartThread(state);
  CHECK(state.gpr[2] == 0U);
  sleeping_thread_entered.wait(false);
  CHECK(!sleeping_thread_resumed.load());
  state.gpr[4] = sleeping_thread_id;
  refract::pspsdk::sceKernelWakeupThread(state);
  CHECK(state.gpr[2] == 0U);
  refract::Runtime::instance().wait_for_guest_threads();
  CHECK(sleeping_thread_resumed.load());

  constexpr std::uint32_t alarm_argument = 0x12345678U;
  state.gpr[4] = 100U;
  state.gpr[5] = alarm_callback_entry;
  state.gpr[6] = alarm_argument;
  refract::pspsdk::sceKernelSetAlarm(state);
  CHECK(state.gpr[2] >= 0x100U);
  for (int attempt = 0; attempt < 1000 && alarm_callback_count.load() == 0U;
       ++attempt)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  CHECK(alarm_callback_count.load() == 1U);
  CHECK(alarm_callback_argument.load() == alarm_argument);

  constexpr std::uint32_t vblank_argument = 0x87654321U;
  state.gpr[4] = 30U;
  state.gpr[5] = 0U;
  state.gpr[6] = vblank_callback_entry;
  state.gpr[7] = vblank_argument;
  refract::pspsdk::sceKernelRegisterSubIntrHandler(state);
  CHECK(state.gpr[2] == 0U);
  state.gpr[4] = 30U;
  state.gpr[5] = 0U;
  refract::pspsdk::sceKernelEnableSubIntr(state);
  CHECK(state.gpr[2] == 0U);
  for (int attempt = 0; attempt < 1000 && vblank_callback_count.load() == 0U;
       ++attempt)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  CHECK(vblank_callback_count.load() != 0U);
  CHECK(vblank_callback_argument.load() == vblank_argument);
  state.gpr[4] = 30U;
  state.gpr[5] = 0U;
  refract::pspsdk::sceKernelReleaseSubIntrHandler(state);
  CHECK(state.gpr[2] == 0U);

  constexpr std::uint32_t ge_callback_data = memory_base + 0x7300U;
  std::fill_n(memory.data() + ge_callback_data - memory_base, 16U, 0U);
  state.gpr[4] = ge_callback_data;
  refract::pspsdk::sceGeSetCallback(state);
  CHECK(state.gpr[2] == 0U);
  state.gpr[4] = state.gpr[2];
  refract::pspsdk::sceGeUnsetCallback(state);
  CHECK(state.gpr[2] == 0U);

  constexpr char module_name[] = "psprism-load-module-by-id-test.prx";
  const auto module_path =
      std::filesystem::temp_directory_path() / module_name;
  {
    std::ofstream module(module_path, std::ios::binary | std::ios::trunc);
    module.put('\0');
  }
  constexpr std::uint32_t module_name_address = memory_base + 0x7400U;
  std::memcpy(memory.data() + module_name_address - memory_base, module_name,
              sizeof(module_name));
  state.gpr[4] = module_name_address;
  state.gpr[5] = 1U;
  state.gpr[6] = 0U;
  refract::pspsdk::sceIoOpen(state);
  const auto module_file_uid = state.gpr[2];
  CHECK(module_file_uid >= 3U);
  state.gpr[4] = module_file_uid;
  state.gpr[5] = 0U;
  state.gpr[6] = 0U;
  refract::pspsdk::sceKernelLoadModuleByID(state);
  const auto module_uid = state.gpr[2];
  CHECK(module_uid >= 0x100U);
  state.gpr[4] = module_uid;
  state.gpr[5] = 0U;
  state.gpr[6] = 0U;
  state.gpr[7] = 0U;
  refract::pspsdk::sceKernelStartModule(state);
  CHECK(state.gpr[2] == 0U);
  state.gpr[4] = module_uid;
  refract::pspsdk::sceKernelUnloadModule(state);
  CHECK(state.gpr[2] == 0U);
  state.gpr[4] = module_file_uid;
  refract::pspsdk::sceIoClose(state);
  CHECK(state.gpr[2] == 0U);
  std::filesystem::remove(module_path);

  constexpr std::uint32_t ringbuffer_address = memory_base + 0x800U;
  constexpr std::uint32_t ringbuffer_data = memory_base + 0xa000U;
  constexpr std::uint32_t ringbuffer_callback = memory_base + 0x6000U;
  constexpr std::uint32_t ringbuffer_gp = 0x08987640U;
  constexpr std::uint32_t mpeg_address = memory_base + 0x9000U;
  constexpr std::uint32_t mpeg_memory = memory_base + 0x10000U;
  constexpr std::uint32_t ringbuffer_owner_canary = 0x51ceba11U;
  store_word(memory, memory_base,
             ringbuffer_address + sizeof(mpeg_state::Ringbuffer),
             ringbuffer_owner_canary);
  state.gpr[4] = ringbuffer_address;
  state.gpr[5] = 2U;
  state.gpr[6] = ringbuffer_data;
  state.gpr[7] = 2U * (2048U + 104U);
  state.gpr[8] = ringbuffer_callback;
  state.gpr[9] = 0x12345678U;
  state.gpr[28] = ringbuffer_gp;
  refract::pspsdk::sceMpegRingbufferConstruct(state);
  CHECK(state.gpr[2] == 0U);
  std::uint32_t observed_ringbuffer_owner{};
  std::memcpy(&observed_ringbuffer_owner,
              memory.data() + ringbuffer_address - memory_base +
                  sizeof(mpeg_state::Ringbuffer),
              sizeof(observed_ringbuffer_owner));
  CHECK(observed_ringbuffer_owner == ringbuffer_owner_canary);
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

  // With no active CALL, the GE ignores RET and continues with the following
  // top-level display-list fragment.
  reset_capture();
  constexpr std::uint32_t empty_ret_address = list_address + 0x100U;
  store_word(memory, memory_base, empty_ret_address, command(0x0bU, 0U));
  (void)write_display_list(memory, memory_base, empty_ret_address + 4U,
                           vertex_address, texture_address, 0x701U, 1U);
  enqueue(state, {empty_ret_address, 0U});
  CHECK(present_count == 1U);
  CHECK(presented_primitives.size() == 1U);

  reset_capture();
  const auto alpha_depth_clear = write_display_list(
      memory, memory_base, list_address + 0x200U, vertex_address,
      texture_address, 0x601U, 1U);
  enqueue(state, alpha_depth_clear);
  CHECK(presented_primitives.size() == 1U);
  CHECK(presented_primitives[0].state.color_write_mask == 0x08U);
  CHECK(presented_primitives[0].state.depth_write);
  CHECK(presented_primitives[0].state.clear_stencil);

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

  // Camera-projected textures use model-space STQ transformed by the GE's
  // dedicated 3x4 texture matrix.  Treating this as ordinary UV mapping makes
  // portal and low-detail geometry appear as flat solid-color polygons.
  constexpr std::uint32_t projected_list_address = memory_base + 0xa000U;
  constexpr std::uint32_t projected_setup_address = memory_base + 0xa200U;
  const std::array<float, 12> texture_matrix{
      2.0F, 0.0F, 0.0F, 0.0F, 3.0F, 0.0F,
      0.0F, 0.0F, 4.0F, 0.25F, 0.5F, 0.75F};
  auto setup_cursor = projected_setup_address;
  store_word(memory, memory_base, setup_cursor, command(0x40U, 0U));
  setup_cursor += 4U;
  for (const auto value : texture_matrix) {
    store_word(memory, memory_base, setup_cursor,
               command(0x41U, std::bit_cast<std::uint32_t>(value) >> 8U));
    setup_cursor += 4U;
  }
  store_word(memory, memory_base, setup_cursor, command(0xc0U, 1U));
  setup_cursor += 4U;
  store_word(memory, memory_base, setup_cursor, command(0x0cU, 0U));
  enqueue(state, {projected_setup_address, 0U});
  const auto projected = write_display_list(
      memory, memory_base, projected_list_address, vertex_address,
      texture_address, 0U, 1U);
  store_word(memory, memory_base, projected_list_address + 2U * 4U,
             command(0x12U, 0x180U));
  reset_capture();
  enqueue(state, projected);
  CHECK(presented_primitives.size() == 1U);
  CHECK(std::abs(presented_primitives[0].first_texture[0] - 20.25F) <
        0.0001F);
  CHECK(std::abs(presented_primitives[0].first_texture[1] - 60.5F) <
        0.0001F);
  CHECK(std::abs(presented_primitives[0].first_texture[2] - 2.75F) <
        0.0001F);

  // Fully masked color draws may still populate depth/stencil for
  // camera-dependent occlusion.  They must remain invisible.
  constexpr std::uint32_t masked_setup_address = memory_base + 0xa400U;
  execute_command_list(
      state, memory, memory_base, masked_setup_address,
      {{0xc0U, 0U}, {0xe8U, 0x00ffffffU}, {0xe9U, 0xffU},
       {0x24U, 1U}, {0xdcU, 6U | (0x5aU << 8U) | (0xf0U << 16U)},
       {0xddU, 2U | (3U << 8U) | (4U << 16U)}});
  reset_capture();
  enqueue(state, normal);
  CHECK(presented_primitives.size() == 1U);
  const auto& masked = presented_primitives[0].state;
  CHECK(masked.color_write_mask == 0U);
  CHECK(masked.stencil_test);
  CHECK(masked.stencil_function == 6U);
  CHECK(masked.stencil_reference == 0x5aU);
  CHECK(masked.stencil_read_mask == 0xf0U);
  CHECK(masked.stencil_write_mask == 0U);
  CHECK(masked.stencil_fail == 2U);
  CHECK(masked.stencil_depth_fail == 3U);
  CHECK(masked.stencil_depth_pass == 4U);

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

  // 1. sceKernelMemset & sceKernelMemcpy with bounds checking
  constexpr std::uint32_t mem_src_addr = memory_base + 0x6100U;
  constexpr std::uint32_t mem_dst_addr = memory_base + 0x6200U;
  std::memset(memory.data() + mem_src_addr - memory_base, 0xaa, 64);
  std::memset(memory.data() + mem_dst_addr - memory_base, 0, 64);
  state.gpr[4] = mem_dst_addr;
  state.gpr[5] = mem_src_addr;
  state.gpr[6] = 32U;
  refract::pspsdk::sceKernelMemcpy(state);
  CHECK(state.gpr[2] == mem_dst_addr);
  for (std::size_t i = 0; i < 32; ++i) {
    CHECK(memory[mem_dst_addr - memory_base + i] == 0xaa);
  }
  CHECK(memory[mem_dst_addr - memory_base + 32] == 0);

  // Out of bounds memcpy (dest + count exceeds memory size)
  state.gpr[4] = memory_base + static_cast<std::uint32_t>(memory.size()) - 10U;
  state.gpr[5] = mem_src_addr;
  state.gpr[6] = 100U;
  refract::pspsdk::sceKernelMemcpy(state);
  CHECK(state.gpr[2] == memory_base + static_cast<std::uint32_t>(memory.size()) - 10U);

  // sceKernelMemset
  state.gpr[4] = mem_dst_addr;
  state.gpr[5] = 0x55U;
  state.gpr[6] = 16U;
  refract::pspsdk::sceKernelMemset(state);
  CHECK(state.gpr[2] == mem_dst_addr);
  for (std::size_t i = 0; i < 16; ++i) {
    CHECK(memory[mem_dst_addr - memory_base + i] == 0x55);
  }

  // 2. sceKernelSysClock2USec, sceKernelSysClock2USecWide, sceKernelUSec2SysClock
  constexpr std::uint32_t clock_addr = memory_base + 0x6300U;
  constexpr std::uint32_t low_addr = memory_base + 0x6310U;
  constexpr std::uint32_t high_addr = memory_base + 0x6314U;
  std::uint64_t test_clock = 0x100000002ULL;
  std::memcpy(memory.data() + clock_addr - memory_base, &test_clock, sizeof(test_clock));
  state.gpr[4] = clock_addr;
  state.gpr[5] = low_addr;
  state.gpr[6] = high_addr;
  refract::pspsdk::sceKernelSysClock2USec(state);
  CHECK(state.gpr[2] == 0U);
  std::uint32_t low_val = 0, high_val = 0;
  std::memcpy(&low_val, memory.data() + low_addr - memory_base, 4);
  std::memcpy(&high_val, memory.data() + high_addr - memory_base, 4);
  CHECK(low_val == 2U);
  CHECK(high_val == 1U);

  state.gpr[4] = 2U;
  state.gpr[5] = 1U;
  state.gpr[6] = low_addr;
  state.gpr[7] = high_addr;
  refract::pspsdk::sceKernelSysClock2USecWide(state);
  CHECK(state.gpr[2] == 0U);
  std::memcpy(&low_val, memory.data() + low_addr - memory_base, 4);
  std::memcpy(&high_val, memory.data() + high_addr - memory_base, 4);
  CHECK(low_val == 2U);
  CHECK(high_val == 1U);

  state.gpr[4] = 0x12345678U;
  state.gpr[5] = clock_addr;
  refract::pspsdk::sceKernelUSec2SysClock(state);
  CHECK(state.gpr[2] == 0U);
  std::memcpy(&test_clock, memory.data() + clock_addr - memory_base, sizeof(test_clock));
  CHECK(test_clock == 0x12345678ULL);

  // 3. scePowerGetBatteryChargePercent, sceKernelChangeThreadPriority
  refract::pspsdk::scePowerGetBatteryChargePercent(state);
  CHECK(state.gpr[2] == 100U);
  state.gpr[4] = 1U;
  state.gpr[5] = 16U;
  refract::pspsdk::sceKernelChangeThreadPriority(state);
  CHECK(state.gpr[2] == 0U);

  // 4. sceIoOpenAsync, sceIoPollAsync non-destructive polling, sceIoWaitAsync
  const auto test_io_dir = std::filesystem::temp_directory_path() / "test_async_io";
  std::filesystem::create_directories(test_io_dir);
  const auto test_file_path = test_io_dir / "async_test.bin";
  {
    std::ofstream ofs(test_file_path, std::ios::binary);
    ofs << "Hello PSP Async IO";
  }
  constexpr std::uint32_t io_path_addr = memory_base + 0x6400U;
  constexpr std::uint32_t async_res_addr = memory_base + 0x6450U;
  const std::string ms0_path = "ms0:/test_async_io/async_test.bin";
  std::memcpy(memory.data() + io_path_addr - memory_base, ms0_path.c_str(), ms0_path.size() + 1);
  state.gpr[4] = io_path_addr;
  state.gpr[5] = 1U; // O_RDONLY
  state.gpr[6] = 0U;
  refract::pspsdk::sceIoOpenAsync(state);
  const auto async_fd = state.gpr[2];
  CHECK(async_fd >= 3U);

  // First poll must succeed and write result without erasing entry
  state.gpr[4] = async_fd;
  state.gpr[5] = async_res_addr;
  refract::pspsdk::sceIoPollAsync(state);
  CHECK(state.gpr[2] == 0U);

  // Second poll must ALSO succeed because poll is non-destructive
  state.gpr[4] = async_fd;
  state.gpr[5] = async_res_addr;
  refract::pspsdk::sceIoPollAsync(state);
  CHECK(state.gpr[2] == 0U);

  // Then wait async consumes the result
  state.gpr[4] = async_fd;
  state.gpr[5] = async_res_addr;
  refract::pspsdk::sceIoWaitAsync(state);
  CHECK(state.gpr[2] == 0U);

  // Close fd
  state.gpr[4] = async_fd;
  refract::pspsdk::sceIoClose(state);
  CHECK(state.gpr[2] == 0U);
  std::filesystem::remove_all(test_io_dir);

  // 5. umd0: path resolution bug test:
  const auto disc_root_path = std::filesystem::temp_directory_path() / "disc";
  std::filesystem::create_directories(disc_root_path);
  const auto umd_file_path = std::filesystem::temp_directory_path() / "test_umd.bin";
  {
    std::ofstream ofs(umd_file_path, std::ios::binary);
    ofs << "UMD DATA";
  }
  const std::string umd_path_str = "umd0:/test_umd.bin";
  std::memcpy(memory.data() + io_path_addr - memory_base, umd_path_str.c_str(), umd_path_str.size() + 1);
  state.gpr[4] = io_path_addr;
  state.gpr[5] = 1U;
  state.gpr[6] = 0U;
  refract::pspsdk::sceIoOpen(state);
  const auto umd_fd = state.gpr[2];
  CHECK(umd_fd >= 3U);
  state.gpr[4] = umd_fd;
  refract::pspsdk::sceIoClose(state);
  CHECK(state.gpr[2] == 0U);
  std::filesystem::remove(umd_file_path);

  return 0;
}
