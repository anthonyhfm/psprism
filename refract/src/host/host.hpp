#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace refract::host {

constexpr std::uint64_t audio_callback_timeout_microseconds(
    std::uint32_t frame_count, std::uint32_t sample_rate) {
  constexpr std::uint64_t minimum_timeout = 100000U;
  constexpr std::uint64_t maximum_timeout = 500000U;
  if (sample_rate == 0U) return minimum_timeout;
  const auto scaled = static_cast<std::uint64_t>(frame_count) * 4000000ULL /
                      sample_rate;
  if (scaled < minimum_timeout) return minimum_timeout;
  if (scaled > maximum_timeout) return maximum_timeout;
  return scaled;
}

std::uint64_t monotonic_microseconds();
std::uint64_t unix_seconds();
void sleep_microseconds(std::uint32_t duration);
bool submit_audio(const std::int16_t* interleaved_stereo,
                  std::uint32_t frame_count,
                  std::uint32_t channel,
                  bool blocking,
                  std::uint32_t sample_rate = 44100U);
using AudioQueuedFramesCallback = std::uint32_t (*)(std::uint32_t);
using AudioResetChannelCallback = void (*)(std::uint32_t);

inline std::atomic<AudioQueuedFramesCallback> audio_queued_frames_callback{};
inline std::atomic<AudioResetChannelCallback> audio_reset_channel_callback{};

inline void set_audio_queue_callbacks(AudioQueuedFramesCallback queued,
                                      AudioResetChannelCallback reset) {
  audio_queued_frames_callback.store(queued, std::memory_order_release);
  audio_reset_channel_callback.store(reset, std::memory_order_release);
}

inline std::uint32_t queued_audio_frames(std::uint32_t channel) {
  const auto callback =
      audio_queued_frames_callback.load(std::memory_order_acquire);
  return callback == nullptr ? 0U : callback(channel);
}

inline void reset_audio_channel(std::uint32_t channel) {
  const auto callback =
      audio_reset_channel_callback.load(std::memory_order_acquire);
  if (callback != nullptr) callback(channel);
}

struct ControllerState {
  std::uint32_t buttons{};
  std::uint8_t analog_x{128};
  std::uint8_t analog_y{128};
};

enum class DialogKind {
  message,
  savedata_load,
  savedata_save,
  savedata_delete,
  osk,
};

struct DialogItem {
  std::string title;
  std::string subtitle;
  std::string detail;
  std::string timestamp;
  std::string size;
  std::vector<std::uint8_t> icon_png;
  std::vector<std::uint8_t> preview_png;
  bool empty{};
};

struct OskField {
  std::string label;
  std::u16string text;
  std::uint32_t limit{};
  std::uint32_t input_type{};
};

struct DialogModel {
  std::uint64_t id{};
  DialogKind kind{DialogKind::message};
  std::string title;
  std::string message;
  std::string detail;
  std::string accept_label{"OK"};
  std::string cancel_label{"Back"};
  std::string yes_label{"Yes"};
  std::string no_label{"No"};
  std::vector<DialogItem> items;
  std::vector<OskField> fields;
  std::size_t selected_item{};
  bool confirm_with_cross{true};
  bool yes_no{};
  bool default_no{};
};

struct DialogResult {
  std::uint64_t id{};
  bool cancelled{};
  bool affirmative{true};
  std::size_t selected_item{};
  std::vector<std::u16string> field_text;
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
  std::uint32_t scissor_left{};
  std::uint32_t scissor_top{};
  std::uint32_t scissor_right{480};
  std::uint32_t scissor_bottom{272};
  std::uint32_t texture_address{};
  bool through_coordinates{};
  bool cull_face{};
  bool front_face_clockwise{true};
  bool depth_test{};
  bool depth_write{};
  std::uint32_t depth_function{1};
  bool alpha_blend{};
  std::uint8_t color_write_mask{0x0fU};
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
  bool texture_color_double{};
  std::uint32_t texture_environment_color{};
};

constexpr std::uint8_t clear_color_write_mask(std::uint32_t clear_mode) {
  constexpr std::uint8_t red = 1U << 0U;
  constexpr std::uint8_t green = 1U << 1U;
  constexpr std::uint8_t blue = 1U << 2U;
  constexpr std::uint8_t alpha = 1U << 3U;
  return static_cast<std::uint8_t>(
      ((clear_mode & 0x100U) != 0U ? red | green | blue : 0U) |
      ((clear_mode & 0x200U) != 0U ? alpha : 0U));
}

constexpr bool texture_color_doubling_enabled(std::uint32_t texture_function) {
  return (texture_function & 0x00010000U) != 0U;
}

void initialize_frontend();
void set_verbose_logging(bool enabled);
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
                         std::shared_ptr<const std::vector<std::uint8_t>>
                             texture = {},
                         std::uint32_t texture_width = 0,
                         std::uint32_t texture_height = 0,
                         GeometryState graphics_state = {});
ControllerState controller_state();
void present_dialog(DialogModel model);
std::optional<DialogResult> poll_dialog_result(std::uint64_t id);
void dismiss_dialog(std::uint64_t id);
bool dialog_visible();

} // namespace refract::host
