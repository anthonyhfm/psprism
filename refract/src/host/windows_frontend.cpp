#include "host.hpp"
#include "ge/ge_framebuffer_source.hpp"
#if defined(REFRACT_HAS_DESKTOP_DIALOGS)
#include "desktop_dialogs.hpp"
#endif

#if !defined(_WIN32)
#error "This psprism frontend requires Windows"
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <xinput.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;

constexpr std::uint32_t psp_select = 0x000001U;
constexpr std::uint32_t psp_start = 0x000008U;
constexpr std::uint32_t psp_up = 0x000010U;
constexpr std::uint32_t psp_right = 0x000020U;
constexpr std::uint32_t psp_down = 0x000040U;
constexpr std::uint32_t psp_left = 0x000080U;
constexpr std::uint32_t psp_l = 0x000100U;
constexpr std::uint32_t psp_r = 0x000200U;
constexpr std::uint32_t psp_triangle = 0x001000U;
constexpr std::uint32_t psp_circle = 0x002000U;
constexpr std::uint32_t psp_cross = 0x004000U;
constexpr std::uint32_t psp_square = 0x008000U;
constexpr std::uint32_t analog_left = 1U << 0U;
constexpr std::uint32_t analog_right = 1U << 1U;
constexpr std::uint32_t analog_up = 1U << 2U;
constexpr std::uint32_t analog_down = 1U << 3U;
constexpr UINT render_message = WM_APP + 1U;
constexpr UINT dialog_message = WM_APP + 2U;
constexpr UINT dialog_dismiss_message = WM_APP + 3U;
constexpr UINT_PTR dialog_timer = 1U;

std::uint32_t normalized_vram_address(std::uint32_t address) {
  if (address < 0x00200000U ||
      (address & 0x0fe00000U) == 0x04000000U)
    return 0x04000000U | (address & 0x001fffffU);
  return address;
}

float render_target_texture_scale(std::uint32_t declared_size,
                                  std::size_t target_size) {
  if (declared_size == 0U || target_size == 0U) return 1.0F;
  return static_cast<float>(declared_size) /
         static_cast<float>(target_size);
}

float render_target_geometry_scale(bool through_coordinates,
                                   std::uint32_t declared_size,
                                   std::size_t target_size) {
  return through_coordinates
             ? render_target_texture_scale(declared_size, target_size)
             : 1.0F;
}

struct RenderTargetAddressOffset {
  std::uint32_t x{};
  std::uint32_t y{};
};

std::optional<RenderTargetAddressOffset> render_target_address_offset(
    std::uint32_t texture_address, std::uint32_t target_address,
    std::uint32_t target_stride, std::size_t target_width,
    std::size_t target_height, std::uint32_t target_format,
    std::uint32_t texture_format) {
  if (target_stride == 0U || target_format != texture_format ||
      texture_address < target_address)
    return std::nullopt;
  const std::uint32_t bytes_per_pixel = target_format == 3U ? 4U : 2U;
  const auto row_bytes = target_stride * bytes_per_pixel;
  const auto byte_offset = texture_address - target_address;
  const auto x_bytes = byte_offset % row_bytes;
  if (x_bytes % bytes_per_pixel != 0U) return std::nullopt;
  const RenderTargetAddressOffset result{x_bytes / bytes_per_pixel,
                                         byte_offset / row_bytes};
  if (result.x >= target_width || result.y >= target_height)
    return std::nullopt;
  return result;
}

std::uint8_t expand4(std::uint32_t value) {
  return static_cast<std::uint8_t>((value << 4U) | value);
}

std::uint8_t expand5(std::uint32_t value) {
  return static_cast<std::uint8_t>((value << 3U) | (value >> 2U));
}

std::vector<std::uint8_t> convert_frame(const std::uint8_t* source,
                                        std::uint32_t stride,
                                        std::uint32_t width,
                                        std::uint32_t height,
                                        std::uint32_t format) {
  std::vector<std::uint8_t> output(static_cast<std::size_t>(width) * height *
                                   4U);
  const auto bytes_per_pixel = format == 3U ? 4U : 2U;
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      const auto source_offset =
          (static_cast<std::size_t>(y) * stride + x) * bytes_per_pixel;
      const auto destination =
          (static_cast<std::size_t>(y) * width + x) * 4U;
      std::uint8_t red{};
      std::uint8_t green{};
      std::uint8_t blue{};
      std::uint8_t alpha{255U};
      if (format == 3U) {
        red = source[source_offset];
        green = source[source_offset + 1U];
        blue = source[source_offset + 2U];
        alpha = source[source_offset + 3U];
      } else {
        const auto packed =
            static_cast<std::uint16_t>(source[source_offset]) |
            static_cast<std::uint16_t>(source[source_offset + 1U]) << 8U;
        if (format == 0U) {
          red = expand5(packed & 31U);
          green = static_cast<std::uint8_t>(((packed >> 5U) & 63U) * 255U /
                                            63U);
          blue = expand5((packed >> 11U) & 31U);
        } else if (format == 1U) {
          red = expand5(packed & 31U);
          green = expand5((packed >> 5U) & 31U);
          blue = expand5((packed >> 10U) & 31U);
          alpha = (packed & 0x8000U) != 0U ? 255U : 0U;
        } else {
          red = expand4(packed & 15U);
          green = expand4((packed >> 4U) & 15U);
          blue = expand4((packed >> 8U) & 15U);
          alpha = expand4((packed >> 12U) & 15U);
        }
      }
      output[destination] = red;
      output[destination + 1U] = green;
      output[destination + 2U] = blue;
      output[destination + 3U] = alpha;
    }
  }
  return output;
}

struct GeometryBatch {
  std::uint32_t type{};
  std::vector<refract::host::GeometryVertex> vertices;
  std::shared_ptr<const std::vector<std::uint8_t>> texture;
  std::uint32_t texture_width{};
  std::uint32_t texture_height{};
  refract::host::GeometryState state;
};

struct CpuFrame {
  std::shared_ptr<const std::vector<std::uint8_t>> pixels;
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint64_t generation{};
};

struct VertexConstants {
  float texture_transform[4]{1.0F, 1.0F, 0.0F, 0.0F};
  float target_scale[2]{1.0F, 1.0F};
  float padding[2]{};
};

struct FragmentConstants {
  std::uint32_t color_test{};
  std::uint32_t color_function{1U};
  std::uint32_t color_reference{};
  std::uint32_t color_mask{0x00ffffffU};
  std::uint32_t alpha_test{};
  std::uint32_t alpha_function{1U};
  std::uint32_t alpha_reference{};
  std::uint32_t alpha_mask{0xffU};
  std::uint32_t texture_function{};
  std::uint32_t texture_alpha_used{1U};
  std::uint32_t texture_color_double{};
  std::uint32_t texture_environment_color{};
  std::uint32_t stencil_alpha_operation{};
  std::uint32_t stencil_reference{};
  std::uint32_t has_texture{};
  std::uint32_t padding{};
};

static_assert(sizeof(VertexConstants) % 16U == 0U);
static_assert(sizeof(FragmentConstants) % 16U == 0U);

std::mutex geometry_mutex;
std::vector<GeometryBatch> building_geometry_batches;
std::vector<GeometryBatch> pending_geometry_batches;
std::vector<GeometryBatch> presented_geometry_batches;
std::mutex cpu_frame_mutex;
std::unordered_map<std::uint32_t, CpuFrame> cpu_frames;
std::uint64_t cpu_frame_generation{};
refract::ge::CacheMetricsAccumulator ge_cache_counters;
refract::ge::FramebufferSourceTracker framebuffer_sources;
std::atomic_uint32_t display_framebuffer_address{0x04000000U};
std::atomic_uint32_t keyboard_buttons{};
std::atomic_uint32_t keyboard_latched_buttons{};
std::atomic_uint32_t keyboard_analog_directions{};
std::atomic_bool verbose_logging{};
std::atomic_bool frontend_exit_requested{};
std::atomic_bool render_message_pending{};
std::once_flag frontend_once;
HWND window_handle{};

std::mutex dialog_mutex;
std::optional<refract::host::DialogModel> pending_dialog;
#if defined(REFRACT_HAS_DESKTOP_DIALOGS)
std::unique_ptr<refract::desktop::DialogFrontend> desktop_dialogs;
std::uint32_t previous_dialog_buttons{};
bool dialog_controller_armed{};
#else
std::optional<refract::host::DialogResult> completed_dialog;
#endif

double window_device_pixel_ratio(HWND window) {
  const auto dpi = window != nullptr ? GetDpiForWindow(window)
                                     : GetDpiForSystem();
  return std::max(1.0, static_cast<double>(dpi) / 96.0);
}

std::uint32_t xinput_buttons(const XINPUT_GAMEPAD& gamepad) {
  std::uint32_t result{};
  const auto buttons = gamepad.wButtons;
  if ((buttons & XINPUT_GAMEPAD_DPAD_UP) != 0U) result |= psp_up;
  if ((buttons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0U) result |= psp_right;
  if ((buttons & XINPUT_GAMEPAD_DPAD_DOWN) != 0U) result |= psp_down;
  if ((buttons & XINPUT_GAMEPAD_DPAD_LEFT) != 0U) result |= psp_left;
  if ((buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0U) result |= psp_l;
  if ((buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0U) result |= psp_r;
  if ((buttons & XINPUT_GAMEPAD_Y) != 0U) result |= psp_triangle;
  if ((buttons & XINPUT_GAMEPAD_B) != 0U) result |= psp_circle;
  if ((buttons & XINPUT_GAMEPAD_A) != 0U) result |= psp_cross;
  if ((buttons & XINPUT_GAMEPAD_X) != 0U) result |= psp_square;
  if ((buttons & XINPUT_GAMEPAD_START) != 0U) result |= psp_start;
  if ((buttons & XINPUT_GAMEPAD_BACK) != 0U) result |= psp_select;
  return result;
}

void update_keyboard_mask(std::atomic_uint32_t& state, std::uint32_t mask,
                          bool pressed) {
  if (pressed)
    state.fetch_or(mask, std::memory_order_relaxed);
  else
    state.fetch_and(~mask, std::memory_order_relaxed);
}

void update_keyboard_button(std::uint32_t mask, bool pressed) {
  update_keyboard_mask(keyboard_buttons, mask, pressed);
  if (pressed) keyboard_latched_buttons.fetch_or(mask, std::memory_order_relaxed);
}

bool update_keyboard_key(WPARAM key, bool pressed) {
  switch (key) {
    case VK_UP: update_keyboard_button(psp_up, pressed); break;
    case VK_RIGHT: update_keyboard_button(psp_right, pressed); break;
    case VK_DOWN: update_keyboard_button(psp_down, pressed); break;
    case VK_LEFT: update_keyboard_button(psp_left, pressed); break;
    case 'Q': update_keyboard_button(psp_l, pressed); break;
    case 'E': update_keyboard_button(psp_r, pressed); break;
    case 'I': update_keyboard_button(psp_triangle, pressed); break;
    case 'L': update_keyboard_button(psp_circle, pressed); break;
    case 'K': update_keyboard_button(psp_cross, pressed); break;
    case 'J': update_keyboard_button(psp_square, pressed); break;
    case VK_RETURN: update_keyboard_button(psp_start, pressed); break;
    case VK_RSHIFT: update_keyboard_button(psp_select, pressed); break;
    case 'A': update_keyboard_mask(keyboard_analog_directions, analog_left,
                                   pressed); break;
    case 'D': update_keyboard_mask(keyboard_analog_directions, analog_right,
                                   pressed); break;
    case 'W': update_keyboard_mask(keyboard_analog_directions, analog_up,
                                   pressed); break;
    case 'S': update_keyboard_mask(keyboard_analog_directions, analog_down,
                                   pressed); break;
    default: return false;
  }
  return true;
}

std::wstring utf16(std::string_view text) {
  if (text.empty()) return {};
  const auto count = MultiByteToWideChar(
      CP_UTF8, 0U, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (count <= 0) return {};
  std::wstring result(static_cast<std::size_t>(count), L'\0');
  MultiByteToWideChar(CP_UTF8, 0U, text.data(),
                      static_cast<int>(text.size()), result.data(), count);
  return result;
}

D3D11_COMPARISON_FUNC comparison_function(std::uint32_t function) {
  constexpr D3D11_COMPARISON_FUNC functions[] = {
      D3D11_COMPARISON_NEVER,         D3D11_COMPARISON_ALWAYS,
      D3D11_COMPARISON_EQUAL,         D3D11_COMPARISON_NOT_EQUAL,
      D3D11_COMPARISON_LESS,          D3D11_COMPARISON_LESS_EQUAL,
      D3D11_COMPARISON_GREATER,       D3D11_COMPARISON_GREATER_EQUAL};
  return functions[std::min(function, 7U)];
}

D3D11_STENCIL_OP stencil_operation(std::uint32_t operation) {
  switch (operation) {
    case 1U: return D3D11_STENCIL_OP_ZERO;
    case 2U: return D3D11_STENCIL_OP_REPLACE;
    case 3U: return D3D11_STENCIL_OP_INVERT;
    case 4U: return D3D11_STENCIL_OP_INCR_SAT;
    case 5U: return D3D11_STENCIL_OP_DECR_SAT;
    default: return D3D11_STENCIL_OP_KEEP;
  }
}

D3D11_BLEND blend_factor(std::uint32_t factor, std::uint32_t fixed,
                         bool source) {
  if (factor >= 10U) {
    if ((fixed & 0x00ffffffU) == 0U) return D3D11_BLEND_ZERO;
    if ((fixed & 0x00ffffffU) == 0x00ffffffU) return D3D11_BLEND_ONE;
    return D3D11_BLEND_BLEND_FACTOR;
  }
  constexpr D3D11_BLEND source_factors[] = {
      D3D11_BLEND_DEST_COLOR, D3D11_BLEND_INV_DEST_COLOR,
      D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA,
      D3D11_BLEND_DEST_ALPHA, D3D11_BLEND_INV_DEST_ALPHA,
      D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA,
      D3D11_BLEND_DEST_ALPHA, D3D11_BLEND_INV_DEST_ALPHA};
  constexpr D3D11_BLEND destination_factors[] = {
      D3D11_BLEND_SRC_COLOR, D3D11_BLEND_INV_SRC_COLOR,
      D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA,
      D3D11_BLEND_DEST_ALPHA, D3D11_BLEND_INV_DEST_ALPHA,
      D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA,
      D3D11_BLEND_DEST_ALPHA, D3D11_BLEND_INV_DEST_ALPHA};
  return source ? source_factors[factor] : destination_factors[factor];
}

D3D11_BLEND_OP blend_operation(std::uint32_t equation) {
  switch (equation) {
    case 1U: return D3D11_BLEND_OP_SUBTRACT;
    case 2U: return D3D11_BLEND_OP_REV_SUBTRACT;
    case 3U: return D3D11_BLEND_OP_MIN;
    case 4U: return D3D11_BLEND_OP_MAX;
    default: return D3D11_BLEND_OP_ADD;
  }
}

std::uint8_t geometry_color_write_mask(
    const refract::host::GeometryState& state) {
  auto mask = state.color_write_mask;
  if (state.render_target_format != 3U ||
      (!state.stencil_test && !state.clear_stencil))
    return mask;
  const auto operation =
      state.clear_stencil ? 2U : state.stencil_depth_pass;
  if ((operation != 1U && operation != 2U) ||
      state.stencil_write_mask != 0xffU)
    mask &= static_cast<std::uint8_t>(~0x08U);
  return mask;
}

const char* shader_source = R"HLSL(
cbuffer VertexState : register(b0) {
  float4 texture_transform;
  float2 target_scale;
  float2 vertex_padding;
};
cbuffer FragmentState : register(b0) {
  uint color_test;
  uint color_function;
  uint color_reference;
  uint color_mask;
  uint alpha_test;
  uint alpha_function;
  uint alpha_reference;
  uint alpha_mask;
  uint texture_function;
  uint texture_alpha_used;
  uint texture_color_double;
  uint texture_environment_color;
  uint stencil_alpha_operation;
  uint stencil_reference;
  uint has_texture;
  uint fragment_padding;
};
Texture2D image_texture : register(t0);
SamplerState image_sampler : register(s0);

struct VertexInput { float4 position : POSITION; float4 color : COLOR0;
                     float3 texcoord : TEXCOORD0; };
struct VertexOutput { float4 position : SV_POSITION; float4 color : COLOR0;
                      float3 texcoord : TEXCOORD0; };
struct DisplayOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

DisplayOutput display_vertex(uint id : SV_VertexID) {
  const float2 positions[3] = {
      float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)};
  const float2 texcoords[3] = {
      float2(0.0, 1.0), float2(0.0, -1.0), float2(2.0, 1.0)};
  DisplayOutput output;
  output.position = float4(positions[id], 0.0, 1.0);
  output.uv = texcoords[id] * texture_transform.xy;
  return output;
}

float4 display_fragment(DisplayOutput input) : SV_TARGET {
  return image_texture.Sample(image_sampler, input.uv);
}

VertexOutput geometry_vertex(VertexInput input) {
  VertexOutput output;
  output.position = input.position;
  output.position.x = input.position.x * target_scale.x +
                      input.position.w * (target_scale.x - 1.0);
  output.position.y = input.position.y * target_scale.y +
                      input.position.w * (1.0 - target_scale.y);
  output.color = input.color;
  output.texcoord = input.texcoord;
  output.texcoord.xy = input.texcoord.xy * texture_transform.xy +
                       texture_transform.zw * input.texcoord.z;
  return output;
}

bool psp_compare(uint value, uint reference, uint function) {
  if (function == 0) return false;
  if (function == 1) return true;
  if (function == 2) return value == reference;
  if (function == 3) return value != reference;
  if (function == 4) return value < reference;
  if (function == 5) return value <= reference;
  if (function == 6) return value > reference;
  return value >= reference;
}

float4 apply_tests(float4 color) {
  uint3 rgb = (uint3)round(saturate(color.rgb) * 255.0);
  uint packed_rgb = rgb.r | (rgb.g << 8) | (rgb.b << 16);
  uint masked_rgb = packed_rgb & color_mask & 0x00ffffff;
  uint masked_reference = color_reference & color_mask & 0x00ffffff;
  bool color_passes = color_function == 1 ||
                      (color_function == 2 && masked_rgb == masked_reference) ||
                      (color_function == 3 && masked_rgb != masked_reference);
  if (color_test != 0 && !color_passes) discard;
  uint alpha = (uint)round(saturate(color.a) * 255.0) & alpha_mask;
  uint reference = alpha_reference & alpha_mask;
  if (alpha_test != 0 && !psp_compare(alpha, reference, alpha_function))
    discard;
  if (stencil_alpha_operation == 1) color.a = 0.0;
  else if (stencil_alpha_operation == 2)
    color.a = (float)stencil_reference / 255.0;
  return color;
}

float4 geometry_fragment(VertexOutput input) : SV_TARGET {
  float4 color = input.color;
  if (has_texture != 0) {
    float q = input.texcoord.z >= 0.0 ? max(input.texcoord.z, 0.000001)
                                      : min(input.texcoord.z, -0.000001);
    float4 texture_color = image_texture.Sample(
        image_sampler, input.texcoord.xy / q);
    bool use_alpha = texture_alpha_used != 0;
    float alpha = use_alpha ? texture_color.a * color.a : color.a;
    if (texture_function == 1)
      color = float4(lerp(color.rgb, texture_color.rgb,
                          use_alpha ? texture_color.a : 1.0), color.a);
    else if (texture_function == 2) {
      float3 environment = float3(
          (float)(texture_environment_color & 0xff) / 255.0,
          (float)((texture_environment_color >> 8) & 0xff) / 255.0,
          (float)((texture_environment_color >> 16) & 0xff) / 255.0);
      color = float4(lerp(color.rgb, environment, texture_color.rgb), alpha);
    } else if (texture_function == 3)
      color = float4(texture_color.rgb,
                     use_alpha ? texture_color.a : color.a);
    else if (texture_function == 4)
      color = float4(texture_color.rgb + color.rgb, alpha);
    else
      color = float4(texture_color.rgb * color.rgb, alpha);
    if (texture_color_double != 0) color.rgb *= 2.0;
  }
  return apply_tests(color);
}
)HLSL";

ComPtr<ID3DBlob> compile_shader(const char* entry, const char* profile) {
  ComPtr<ID3DBlob> bytecode;
  ComPtr<ID3DBlob> errors;
  const auto flags = D3DCOMPILE_ENABLE_STRICTNESS |
#if defined(_DEBUG)
                     D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
                     D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
  const auto result = D3DCompile(shader_source, std::strlen(shader_source),
                                 "refract_d3d11.hlsl", nullptr, nullptr,
                                 entry, profile, flags, 0U, &bytecode, &errors);
  if (FAILED(result)) {
    std::fprintf(stderr, "psprism: D3D shader %s failed: %s\n", entry,
                 errors == nullptr
                     ? "unknown error"
                     : static_cast<const char*>(errors->GetBufferPointer()));
    return {};
  }
  return bytecode;
}

struct TextureResource {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> view;
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint64_t generation{};
};

struct RenderTarget {
  TextureResource color;
  ComPtr<ID3D11RenderTargetView> target;
  ComPtr<ID3D11Texture2D> depth;
  ComPtr<ID3D11DepthStencilView> depth_view;
  std::uint32_t stride{};
  std::uint32_t format{};
  std::uint32_t depth_address{};
};

class Renderer {
public:
  bool initialize(HWND window) {
    DXGI_SWAP_CHAIN_DESC swap_description{};
    swap_description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_description.SampleDesc.Count = 1U;
    swap_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_description.BufferCount = 2U;
    swap_description.OutputWindow = window;
    swap_description.Windowed = TRUE;
    swap_description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    constexpr D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL selected{};
    auto result = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
        static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
        &swap_description, &swap_chain_, &device_, &selected, &context_);
#if defined(_DEBUG)
    if (result == DXGI_ERROR_SDK_COMPONENT_MISSING) {
      flags &= ~D3D11_CREATE_DEVICE_DEBUG;
      result = D3D11CreateDeviceAndSwapChain(
          nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
          static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
          &swap_description, &swap_chain_, &device_, &selected, &context_);
    }
#endif
    if (FAILED(result)) {
      swap_chain_.Reset();
      device_.Reset();
      context_.Reset();
      const auto hardware_result = result;
      result = D3D11CreateDeviceAndSwapChain(
          nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
          flags & ~D3D11_CREATE_DEVICE_DEBUG, levels,
          static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
          &swap_description, &swap_chain_, &device_, &selected, &context_);
      if (SUCCEEDED(result))
        std::fprintf(stderr,
                     "psprism: D3D11 hardware device unavailable (%08lx); "
                     "using WARP\n",
                     static_cast<unsigned long>(hardware_result));
    }
    if (FAILED(result)) {
      std::fprintf(stderr, "psprism: D3D11 device creation failed: %08lx\n",
                   static_cast<unsigned long>(result));
      return false;
    }

    const auto display_vs = compile_shader("display_vertex", "vs_5_0");
    const auto display_ps = compile_shader("display_fragment", "ps_5_0");
    const auto geometry_vs = compile_shader("geometry_vertex", "vs_5_0");
    const auto geometry_ps = compile_shader("geometry_fragment", "ps_5_0");
    if (display_vs == nullptr || display_ps == nullptr ||
        geometry_vs == nullptr || geometry_ps == nullptr)
      return false;
    if (FAILED(device_->CreateVertexShader(
            display_vs->GetBufferPointer(), display_vs->GetBufferSize(),
            nullptr, &display_vertex_shader_)) ||
        FAILED(device_->CreatePixelShader(
            display_ps->GetBufferPointer(), display_ps->GetBufferSize(),
            nullptr, &display_pixel_shader_)) ||
        FAILED(device_->CreateVertexShader(
            geometry_vs->GetBufferPointer(), geometry_vs->GetBufferSize(),
            nullptr, &geometry_vertex_shader_)) ||
        FAILED(device_->CreatePixelShader(
            geometry_ps->GetBufferPointer(), geometry_ps->GetBufferSize(),
            nullptr, &geometry_pixel_shader_)))
      return false;

    constexpr D3D11_INPUT_ELEMENT_DESC input_elements[] = {
        {"POSITION", 0U, DXGI_FORMAT_R32G32B32A32_FLOAT, 0U, 0U,
         D3D11_INPUT_PER_VERTEX_DATA, 0U},
        {"COLOR", 0U, DXGI_FORMAT_R32G32B32A32_FLOAT, 0U, 16U,
         D3D11_INPUT_PER_VERTEX_DATA, 0U},
        {"TEXCOORD", 0U, DXGI_FORMAT_R32G32B32_FLOAT, 0U, 32U,
         D3D11_INPUT_PER_VERTEX_DATA, 0U}};
    if (FAILED(device_->CreateInputLayout(
            input_elements, static_cast<UINT>(std::size(input_elements)),
            geometry_vs->GetBufferPointer(), geometry_vs->GetBufferSize(),
            &input_layout_)))
      return false;

    if (!create_dynamic_buffer(sizeof(VertexConstants),
                               D3D11_BIND_CONSTANT_BUFFER,
                               vertex_constants_) ||
        !create_dynamic_buffer(sizeof(FragmentConstants),
                               D3D11_BIND_CONSTANT_BUFFER,
                               fragment_constants_))
      return false;

    for (std::size_t index = 0U; index < samplers_.size(); ++index) {
      D3D11_SAMPLER_DESC description{};
      const bool min_linear = (index & 8U) != 0U;
      const bool mag_linear = (index & 4U) != 0U;
      description.Filter = min_linear || mag_linear
                               ? D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT
                               : D3D11_FILTER_MIN_MAG_MIP_POINT;
      description.AddressU = (index & 1U) != 0U
                                 ? D3D11_TEXTURE_ADDRESS_CLAMP
                                 : D3D11_TEXTURE_ADDRESS_WRAP;
      description.AddressV = (index & 2U) != 0U
                                 ? D3D11_TEXTURE_ADDRESS_CLAMP
                                 : D3D11_TEXTURE_ADDRESS_WRAP;
      description.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
      description.MaxLOD = D3D11_FLOAT32_MAX;
      if (FAILED(device_->CreateSamplerState(&description, &samplers_[index])))
        return false;
    }
#if defined(REFRACT_HAS_DESKTOP_DIALOGS)
    D3D11_BLEND_DESC dialog_blend_description{};
    auto& dialog_blend = dialog_blend_description.RenderTarget[0];
    dialog_blend.BlendEnable = TRUE;
    dialog_blend.SrcBlend = D3D11_BLEND_ONE;
    dialog_blend.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    dialog_blend.BlendOp = D3D11_BLEND_OP_ADD;
    dialog_blend.SrcBlendAlpha = D3D11_BLEND_ONE;
    dialog_blend.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    dialog_blend.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    dialog_blend.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device_->CreateBlendState(&dialog_blend_description,
                                         &dialog_blend_state_)))
      return false;
#endif
    return rebuild_back_buffer();
  }

  void resize() {
    if (swap_chain_ == nullptr) return;
    context_->OMSetRenderTargets(0U, nullptr, nullptr);
    back_buffer_.Reset();
    if (SUCCEEDED(swap_chain_->ResizeBuffers(0U, 0U, 0U,
                                              DXGI_FORMAT_UNKNOWN, 0U)))
      rebuild_back_buffer();
  }

  void draw() {
    if (context_ == nullptr || back_buffer_ == nullptr) return;
    std::vector<GeometryBatch> batches;
    {
      std::lock_guard lock(geometry_mutex);
      batches.swap(presented_geometry_batches);
    }
    draw_geometry(batches);
    draw_display();
  }

private:
  bool create_dynamic_buffer(std::size_t size, UINT bind,
                             ComPtr<ID3D11Buffer>& output) {
    D3D11_BUFFER_DESC description{};
    description.ByteWidth = static_cast<UINT>((size + 15U) & ~15U);
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = bind;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    return SUCCEEDED(device_->CreateBuffer(&description, nullptr, &output));
  }

  template <typename T>
  void upload_constants(ID3D11Buffer* buffer, const T& value) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context_->Map(buffer, 0U, D3D11_MAP_WRITE_DISCARD, 0U,
                                &mapped))) {
      std::memcpy(mapped.pData, &value, sizeof(value));
      context_->Unmap(buffer, 0U);
    }
  }

  bool rebuild_back_buffer() {
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(swap_chain_->GetBuffer(0U, IID_PPV_ARGS(&texture))))
      return false;
    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);
    back_width_ = description.Width;
    back_height_ = description.Height;
    return SUCCEEDED(device_->CreateRenderTargetView(texture.Get(), nullptr,
                                                      &back_buffer_));
  }

  TextureResource create_texture(std::uint32_t width, std::uint32_t height,
                                 const std::uint8_t* pixels,
                                 std::uint64_t generation = 0U) {
    TextureResource result;
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1U;
    description.ArraySize = 1U;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1U;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = pixels;
    initial.SysMemPitch = width * 4U;
    if (FAILED(device_->CreateTexture2D(&description,
                                        pixels == nullptr ? nullptr : &initial,
                                        &result.texture)) ||
        FAILED(device_->CreateShaderResourceView(result.texture.Get(), nullptr,
                                                  &result.view)))
      return {};
    result.width = width;
    result.height = height;
    result.generation = generation;
    return result;
  }

  RenderTarget& ensure_render_target(std::uint32_t address,
                                     const GeometryBatch& batch) {
    auto& target = render_targets_[address];
    const auto width = std::max<std::uint32_t>(
        target.color.width, batch.state.render_target_width);
    const auto height = std::max<std::uint32_t>(
        target.color.height, batch.state.render_target_height);
    if (target.color.texture == nullptr || target.color.width < width ||
        target.color.height < height) {
      target = {};
      D3D11_TEXTURE2D_DESC color_description{};
      color_description.Width = width;
      color_description.Height = height;
      color_description.MipLevels = 1U;
      color_description.ArraySize = 1U;
      color_description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      color_description.SampleDesc.Count = 1U;
      color_description.Usage = D3D11_USAGE_DEFAULT;
      color_description.BindFlags = D3D11_BIND_RENDER_TARGET |
                                    D3D11_BIND_SHADER_RESOURCE;
      device_->CreateTexture2D(&color_description, nullptr,
                               &target.color.texture);
      device_->CreateRenderTargetView(target.color.texture.Get(), nullptr,
                                      &target.target);
      device_->CreateShaderResourceView(target.color.texture.Get(), nullptr,
                                        &target.color.view);
      target.color.width = width;
      target.color.height = height;

      D3D11_TEXTURE2D_DESC depth_description = color_description;
      depth_description.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
      depth_description.BindFlags = D3D11_BIND_DEPTH_STENCIL;
      device_->CreateTexture2D(&depth_description, nullptr, &target.depth);
      device_->CreateDepthStencilView(target.depth.Get(), nullptr,
                                      &target.depth_view);
      constexpr float clear[4]{};
      context_->ClearRenderTargetView(target.target.Get(), clear);
      context_->ClearDepthStencilView(target.depth_view.Get(),
                                      D3D11_CLEAR_DEPTH |
                                          D3D11_CLEAR_STENCIL,
                                      1.0F, 0U);
    }
    target.stride = batch.state.render_target_stride;
    target.format = batch.state.render_target_format;
    target.depth_address =
        normalized_vram_address(batch.state.depth_target_address);
    return target;
  }

  ComPtr<ID3D11BlendState> blend_state(
      const refract::host::GeometryState& state) {
    std::uint64_t key = state.alpha_blend ? 1ULL : 0ULL;
    key |= static_cast<std::uint64_t>(state.blend_source & 0xfU) << 1U;
    key |= static_cast<std::uint64_t>(state.blend_destination & 0xfU) << 5U;
    key |= static_cast<std::uint64_t>(state.blend_equation & 7U) << 9U;
    key |= static_cast<std::uint64_t>(geometry_color_write_mask(state)) << 12U;
    key |= static_cast<std::uint64_t>(state.blend_fix_a & 0x00ffffffU) << 16U;
    key ^= static_cast<std::uint64_t>(state.blend_fix_b & 0x00ffffffU) << 40U;
    if (const auto found = blend_states_.find(key);
        found != blend_states_.end()) {
      ge_cache_counters.record_pipeline(true);
      return found->second;
    }
    ge_cache_counters.record_pipeline(false);
    D3D11_BLEND_DESC description{};
    auto& target = description.RenderTarget[0];
    target.BlendEnable = state.alpha_blend;
    target.SrcBlend = blend_factor(state.blend_source, state.blend_fix_a, true);
    target.DestBlend = blend_factor(state.blend_destination,
                                    state.blend_fix_b, false);
    target.BlendOp = blend_operation(state.blend_equation);
    target.SrcBlendAlpha = target.SrcBlend;
    target.DestBlendAlpha = target.DestBlend;
    target.BlendOpAlpha = target.BlendOp;
    target.RenderTargetWriteMask = geometry_color_write_mask(state);
    ComPtr<ID3D11BlendState> result;
    device_->CreateBlendState(&description, &result);
    if (blend_states_.size() >= 512U) blend_states_.clear();
    blend_states_.emplace(key, result);
    return result;
  }

  ComPtr<ID3D11DepthStencilState> depth_state(
      const refract::host::GeometryState& state) {
    std::uint64_t key = state.depth_write ? 1ULL : 0ULL;
    key |= (state.depth_test ? 1ULL : 0ULL) << 1U;
    key |= static_cast<std::uint64_t>(state.depth_function & 7U) << 2U;
    key |= (state.stencil_test ? 1ULL : 0ULL) << 5U;
    key |= (state.clear_stencil ? 1ULL : 0ULL) << 6U;
    key |= static_cast<std::uint64_t>(state.stencil_function & 7U) << 7U;
    key |= static_cast<std::uint64_t>(state.stencil_fail & 7U) << 10U;
    key |= static_cast<std::uint64_t>(state.stencil_depth_fail & 7U) << 13U;
    key |= static_cast<std::uint64_t>(state.stencil_depth_pass & 7U) << 16U;
    key |= static_cast<std::uint64_t>(state.stencil_read_mask & 0xffU) << 19U;
    key |= static_cast<std::uint64_t>(state.stencil_write_mask & 0xffU) << 27U;
    if (const auto found = depth_states_.find(key);
        found != depth_states_.end())
      return found->second;
    D3D11_DEPTH_STENCIL_DESC description{};
    description.DepthEnable = state.depth_test || state.depth_write;
    description.DepthWriteMask = state.depth_write
                                     ? D3D11_DEPTH_WRITE_MASK_ALL
                                     : D3D11_DEPTH_WRITE_MASK_ZERO;
    description.DepthFunc = state.depth_test
                                ? comparison_function(state.depth_function)
                                : D3D11_COMPARISON_ALWAYS;
    description.StencilEnable = state.stencil_test || state.clear_stencil;
    description.StencilReadMask = static_cast<UINT8>(
        state.clear_stencil ? 0xffU : state.stencil_read_mask);
    description.StencilWriteMask =
        static_cast<UINT8>(state.stencil_write_mask);
    auto stencil = D3D11_DEPTH_STENCILOP_DESC{};
    stencil.StencilFunc = state.clear_stencil
                              ? D3D11_COMPARISON_ALWAYS
                              : comparison_function(state.stencil_function);
    stencil.StencilFailOp = state.clear_stencil
                                ? D3D11_STENCIL_OP_REPLACE
                                : stencil_operation(state.stencil_fail);
    stencil.StencilDepthFailOp = state.clear_stencil
                                     ? D3D11_STENCIL_OP_REPLACE
                                     : stencil_operation(
                                           state.stencil_depth_fail);
    stencil.StencilPassOp = state.clear_stencil
                                ? D3D11_STENCIL_OP_REPLACE
                                : stencil_operation(state.stencil_depth_pass);
    description.FrontFace = stencil;
    description.BackFace = stencil;
    ComPtr<ID3D11DepthStencilState> result;
    device_->CreateDepthStencilState(&description, &result);
    depth_states_.emplace(key, result);
    return result;
  }

  ID3D11RasterizerState* rasterizer_state(
      const refract::host::GeometryState& state) {
    const auto index = (state.cull_face ? 1U : 0U) |
                       (state.front_face_clockwise ? 2U : 0U);
    if (rasterizers_[index] == nullptr) {
      D3D11_RASTERIZER_DESC description{};
      description.FillMode = D3D11_FILL_SOLID;
      description.CullMode = state.cull_face ? D3D11_CULL_BACK
                                             : D3D11_CULL_NONE;
      description.FrontCounterClockwise = !state.front_face_clockwise;
      description.DepthClipEnable = TRUE;
      description.ScissorEnable = TRUE;
      device_->CreateRasterizerState(&description, &rasterizers_[index]);
    }
    return rasterizers_[index].Get();
  }

  TextureResource* uploaded_texture(const GeometryBatch& batch) {
    if (batch.texture == nullptr || batch.texture->empty() ||
        batch.texture_width == 0U || batch.texture_height == 0U)
      return nullptr;
    std::uint64_t key = batch.state.texture_address;
    key ^= static_cast<std::uint64_t>(batch.texture_width) << 32U;
    key ^= static_cast<std::uint64_t>(batch.texture_height) << 48U;
    auto& texture = uploaded_textures_[key];
    const bool hit = texture.texture != nullptr &&
                     texture.generation == batch.state.texture_content_hash;
    ge_cache_counters.record_texture(
        hit, static_cast<std::uint64_t>(batch.texture_width) *
                 batch.texture_height * 4U);
    if (!hit)
      texture = create_texture(batch.texture_width, batch.texture_height,
                               batch.texture->data(),
                               batch.state.texture_content_hash);
    if (uploaded_textures_.size() > 2048U) uploaded_textures_.clear();
    return texture.texture == nullptr ? nullptr : &texture;
  }

  void draw_geometry(const std::vector<GeometryBatch>& batches) {
    std::vector<UINT> vertex_offsets(batches.size());
    std::size_t required_vertex_bytes{};
    for (std::size_t index = 0U; index < batches.size(); ++index) {
      required_vertex_bytes =
          (required_vertex_bytes + 15U) & ~std::size_t{15U};
      if (required_vertex_bytes > std::numeric_limits<UINT>::max()) return;
      vertex_offsets[index] = static_cast<UINT>(required_vertex_bytes);
      const auto byte_count = batches[index].vertices.size() *
                              sizeof(refract::host::GeometryVertex);
      if (byte_count > std::numeric_limits<UINT>::max() -
                           required_vertex_bytes)
        return;
      required_vertex_bytes += byte_count;
    }
    if (required_vertex_bytes == 0U) return;
    const bool reused = frame_vertex_buffer_ != nullptr &&
                        frame_vertex_buffer_capacity_ >= required_vertex_bytes;
    if (!reused) {
      constexpr std::size_t initial_vertex_buffer_size = 4U << 20U;
      const auto allocation_size =
          std::max(required_vertex_bytes, initial_vertex_buffer_size);
      frame_vertex_buffer_.Reset();
      if (!create_dynamic_buffer(allocation_size, D3D11_BIND_VERTEX_BUFFER,
                                 frame_vertex_buffer_))
        return;
      frame_vertex_buffer_capacity_ = allocation_size;
    }
    D3D11_MAPPED_SUBRESOURCE vertex_mapping{};
    if (FAILED(context_->Map(frame_vertex_buffer_.Get(), 0U,
                             D3D11_MAP_WRITE_DISCARD, 0U, &vertex_mapping)))
      return;
    for (std::size_t index = 0U; index < batches.size(); ++index) {
      const auto& vertices = batches[index].vertices;
      if (vertices.empty()) continue;
      std::memcpy(static_cast<std::uint8_t*>(vertex_mapping.pData) +
                      vertex_offsets[index],
                  vertices.data(),
                  vertices.size() * sizeof(refract::host::GeometryVertex));
    }
    context_->Unmap(frame_vertex_buffer_.Get(), 0U);
    ge_cache_counters.record_vertex_buffer(reused, required_vertex_bytes);

    ID3D11ShaderResourceView* no_view{};
    for (std::size_t batch_index = 0U; batch_index < batches.size();
         ++batch_index) {
      const auto& batch = batches[batch_index];
      if (batch.vertices.empty()) continue;
      const auto address =
          normalized_vram_address(batch.state.render_target_address);
      auto& target = ensure_render_target(address, batch);
      if (target.target == nullptr || target.depth_view == nullptr) continue;
      context_->PSSetShaderResources(0U, 1U, &no_view);
      ID3D11RenderTargetView* target_view = target.target.Get();
      context_->OMSetRenderTargets(1U, &target_view, target.depth_view.Get());
      const D3D11_VIEWPORT viewport{
          0.0F, 0.0F, static_cast<float>(target.color.width),
          static_cast<float>(target.color.height), 0.0F, 1.0F};
      context_->RSSetViewports(1U, &viewport);
      const D3D11_RECT scissor{
          static_cast<LONG>(std::min(batch.state.scissor_left,
                                     target.color.width)),
          static_cast<LONG>(std::min(batch.state.scissor_top,
                                     target.color.height)),
          static_cast<LONG>(std::min(batch.state.scissor_right,
                                     target.color.width)),
          static_cast<LONG>(std::min(batch.state.scissor_bottom,
                                     target.color.height))};
      if (scissor.right <= scissor.left || scissor.bottom <= scissor.top)
        continue;
      context_->RSSetScissorRects(1U, &scissor);
      context_->RSSetState(rasterizer_state(batch.state));

      const auto blend = blend_state(batch.state);
      const auto fixed = batch.state.blend_source >= 10U
                             ? batch.state.blend_fix_a
                             : batch.state.blend_fix_b;
      const float blend_color[4]{
          static_cast<float>(fixed & 0xffU) / 255.0F,
          static_cast<float>((fixed >> 8U) & 0xffU) / 255.0F,
          static_cast<float>((fixed >> 16U) & 0xffU) / 255.0F, 1.0F};
      context_->OMSetBlendState(blend.Get(), blend_color, 0xffffffffU);
      const auto depth = depth_state(batch.state);
      context_->OMSetDepthStencilState(
          depth.Get(), batch.state.stencil_reference & 0xffU);

      TextureResource* sampled{};
      VertexConstants vertex_state;
      const auto texture_address =
          normalized_vram_address(batch.state.texture_address);
      std::uint32_t closest_offset = UINT32_MAX;
      for (auto& [candidate_address, candidate] : render_targets_) {
        const auto offset = render_target_address_offset(
            texture_address, candidate_address, candidate.stride,
            candidate.color.width, candidate.color.height, candidate.format,
            batch.state.texture_format);
        const auto byte_offset = texture_address - candidate_address;
        if (!offset || byte_offset >= closest_offset ||
            candidate.color.view == nullptr)
          continue;
        closest_offset = byte_offset;
        sampled = &candidate.color;
        vertex_state.texture_transform[0] = render_target_texture_scale(
            batch.texture_width, candidate.color.width);
        vertex_state.texture_transform[1] = render_target_texture_scale(
            batch.texture_height, candidate.color.height);
        vertex_state.texture_transform[2] =
            static_cast<float>(offset->x) / candidate.color.width;
        vertex_state.texture_transform[3] =
            static_cast<float>(offset->y) / candidate.color.height;
      }
      if (sampled == nullptr) sampled = uploaded_texture(batch);
      vertex_state.target_scale[0] = render_target_geometry_scale(
          batch.state.through_coordinates, batch.state.render_target_width,
          target.color.width);
      vertex_state.target_scale[1] = render_target_geometry_scale(
          batch.state.through_coordinates, batch.state.render_target_height,
          target.color.height);
      upload_constants(vertex_constants_.Get(), vertex_state);

      const FragmentConstants fragment_state{
          batch.state.color_test ? 1U : 0U,
          batch.state.color_function,
          batch.state.color_reference,
          batch.state.color_mask,
          batch.state.alpha_test ? 1U : 0U,
          batch.state.alpha_function,
          batch.state.alpha_reference,
          batch.state.alpha_mask,
          batch.state.texture_function,
          batch.state.texture_alpha_used ? 1U : 0U,
          batch.state.texture_color_double ? 1U : 0U,
          batch.state.texture_environment_color,
          batch.state.render_target_format == 3U &&
                  (batch.state.stencil_test || batch.state.clear_stencil) &&
                  batch.state.stencil_write_mask == 0xffU
              ? (batch.state.clear_stencil ? 2U
                                           : batch.state.stencil_depth_pass)
              : 0U,
          batch.state.stencil_reference & 0xffU,
          sampled == nullptr ? 0U : 1U,
          0U};
      upload_constants(fragment_constants_.Get(), fragment_state);

      const UINT stride = sizeof(refract::host::GeometryVertex);
      const UINT offset = vertex_offsets[batch_index];
      ID3D11Buffer* vertex_buffer = frame_vertex_buffer_.Get();
      context_->IASetInputLayout(input_layout_.Get());
      context_->IASetVertexBuffers(0U, 1U, &vertex_buffer, &stride, &offset);
      constexpr D3D11_PRIMITIVE_TOPOLOGY topologies[] = {
          D3D11_PRIMITIVE_TOPOLOGY_POINTLIST,
          D3D11_PRIMITIVE_TOPOLOGY_LINELIST,
          D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP,
          D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
          D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP};
      context_->IASetPrimitiveTopology(topologies[batch.type]);
      context_->VSSetShader(geometry_vertex_shader_.Get(), nullptr, 0U);
      ID3D11Buffer* vertex_constants = vertex_constants_.Get();
      context_->VSSetConstantBuffers(0U, 1U, &vertex_constants);
      context_->PSSetShader(geometry_pixel_shader_.Get(), nullptr, 0U);
      ID3D11Buffer* fragment_constants = fragment_constants_.Get();
      context_->PSSetConstantBuffers(0U, 1U, &fragment_constants);
      if (sampled != nullptr) {
        ID3D11ShaderResourceView* view = sampled->view.Get();
        context_->PSSetShaderResources(0U, 1U, &view);
        const auto sampler_index =
            (batch.state.texture_min_linear ? 8U : 0U) |
            (batch.state.texture_mag_linear ? 4U : 0U) |
            (batch.state.texture_clamp_t ? 2U : 0U) |
            (batch.state.texture_clamp_s ? 1U : 0U);
        ID3D11SamplerState* sampler = samplers_[sampler_index].Get();
        context_->PSSetSamplers(0U, 1U, &sampler);
      }
      context_->Draw(static_cast<UINT>(batch.vertices.size()), 0U);
    }
    context_->PSSetShaderResources(0U, 1U, &no_view);
  }

  TextureResource* display_texture(std::uint32_t address) {
    if (framebuffer_sources.cpu_is_latest(address)) {
      CpuFrame frame;
      {
        std::lock_guard lock(cpu_frame_mutex);
        const auto found = cpu_frames.find(address);
        if (found == cpu_frames.end()) return nullptr;
        frame = found->second;
      }
      auto& texture = cpu_textures_[address];
      if (texture.texture == nullptr || texture.width != frame.width ||
          texture.height != frame.height) {
        texture = create_texture(frame.width, frame.height,
                                 frame.pixels->data(), frame.generation);
      } else if (texture.generation != frame.generation) {
        context_->UpdateSubresource(texture.texture.Get(), 0U, nullptr,
                                    frame.pixels->data(), frame.width * 4U,
                                    0U);
        texture.generation = frame.generation;
      }
      return texture.texture == nullptr ? nullptr : &texture;
    }
    const auto found = render_targets_.find(address);
    return found == render_targets_.end() ? nullptr : &found->second.color;
  }

  void draw_display() {
    ID3D11RenderTargetView* back_buffer = back_buffer_.Get();
    context_->OMSetRenderTargets(1U, &back_buffer, nullptr);
    constexpr float clear_color[4]{0.02F, 0.02F, 0.025F, 1.0F};
    context_->ClearRenderTargetView(back_buffer_.Get(), clear_color);
    const float target_aspect = 480.0F / 272.0F;
    float width = static_cast<float>(back_width_);
    float height = width / target_aspect;
    if (height > back_height_) {
      height = static_cast<float>(back_height_);
      width = height * target_aspect;
    }
    const D3D11_VIEWPORT viewport{
        (static_cast<float>(back_width_) - width) * 0.5F,
        (static_cast<float>(back_height_) - height) * 0.5F,
        width, height, 0.0F, 1.0F};
    context_->RSSetViewports(1U, &viewport);
    context_->RSSetState(nullptr);
    context_->OMSetBlendState(nullptr, nullptr, 0xffffffffU);
    context_->OMSetDepthStencilState(nullptr, 0U);
    auto* texture = display_texture(normalized_vram_address(
        display_framebuffer_address.load(std::memory_order_relaxed)));
    if (texture != nullptr) {
      VertexConstants display_state;
      display_state.texture_transform[0] =
          std::min(1.0F, 480.0F / static_cast<float>(texture->width));
      display_state.texture_transform[1] =
          std::min(1.0F, 272.0F / static_cast<float>(texture->height));
      upload_constants(vertex_constants_.Get(), display_state);
      context_->IASetInputLayout(nullptr);
      context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      context_->VSSetShader(display_vertex_shader_.Get(), nullptr, 0U);
      ID3D11Buffer* vertex_constants = vertex_constants_.Get();
      context_->VSSetConstantBuffers(0U, 1U, &vertex_constants);
      context_->PSSetShader(display_pixel_shader_.Get(), nullptr, 0U);
      ID3D11ShaderResourceView* view = texture->view.Get();
      context_->PSSetShaderResources(0U, 1U, &view);
      ID3D11SamplerState* sampler = samplers_[3U].Get();
      context_->PSSetSamplers(0U, 1U, &sampler);
      context_->Draw(3U, 0U);
      ID3D11ShaderResourceView* no_view{};
      context_->PSSetShaderResources(0U, 1U, &no_view);
    }
#if defined(REFRACT_HAS_DESKTOP_DIALOGS)
    if (desktop_dialogs != nullptr && desktop_dialogs->visible()) {
      const auto scale = window_device_pixel_ratio(window_handle);
      const auto dialog_frame = desktop_dialogs->rendered_frame(
          scale,
          static_cast<std::uint32_t>(
              std::ceil(static_cast<double>(back_width_) / scale)),
          static_cast<std::uint32_t>(
              std::ceil(static_cast<double>(back_height_) / scale)));
      if (!dialog_frame.pixels.empty()) {
        if (dialog_texture_.texture == nullptr ||
            dialog_texture_.width != dialog_frame.width ||
            dialog_texture_.height != dialog_frame.height) {
          dialog_texture_ = create_texture(
              dialog_frame.width, dialog_frame.height,
              dialog_frame.pixels.data());
        } else {
          context_->UpdateSubresource(dialog_texture_.texture.Get(), 0U,
                                      nullptr, dialog_frame.pixels.data(),
                                      dialog_frame.width * 4U, 0U);
        }
        if (dialog_texture_.view != nullptr) {
          const D3D11_VIEWPORT dialog_viewport{
              0.0F, 0.0F, static_cast<float>(back_width_),
              static_cast<float>(back_height_), 0.0F, 1.0F};
          context_->RSSetViewports(1U, &dialog_viewport);
          context_->OMSetBlendState(dialog_blend_state_.Get(), nullptr,
                                    0xffffffffU);
          VertexConstants dialog_state;
          upload_constants(vertex_constants_.Get(), dialog_state);
          ID3D11ShaderResourceView* dialog_view = dialog_texture_.view.Get();
          context_->PSSetShaderResources(0U, 1U, &dialog_view);
          context_->Draw(3U, 0U);
          ID3D11ShaderResourceView* no_view{};
          context_->PSSetShaderResources(0U, 1U, &no_view);
          context_->OMSetBlendState(nullptr, nullptr, 0xffffffffU);
        }
      }
    }
#endif
    const auto result = swap_chain_->Present(1U, 0U);
    if (FAILED(result) && verbose_logging.load(std::memory_order_relaxed))
      std::fprintf(stderr, "psprism: DXGI present failed: %08lx\n",
                   static_cast<unsigned long>(result));
  }

  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> context_;
  ComPtr<IDXGISwapChain> swap_chain_;
  ComPtr<ID3D11RenderTargetView> back_buffer_;
  ComPtr<ID3D11VertexShader> display_vertex_shader_;
  ComPtr<ID3D11PixelShader> display_pixel_shader_;
  ComPtr<ID3D11VertexShader> geometry_vertex_shader_;
  ComPtr<ID3D11PixelShader> geometry_pixel_shader_;
#if defined(REFRACT_HAS_DESKTOP_DIALOGS)
  ComPtr<ID3D11BlendState> dialog_blend_state_;
  TextureResource dialog_texture_;
#endif
  ComPtr<ID3D11InputLayout> input_layout_;
  ComPtr<ID3D11Buffer> frame_vertex_buffer_;
  ComPtr<ID3D11Buffer> vertex_constants_;
  ComPtr<ID3D11Buffer> fragment_constants_;
  std::array<ComPtr<ID3D11SamplerState>, 16U> samplers_;
  std::array<ComPtr<ID3D11RasterizerState>, 4U> rasterizers_;
  std::unordered_map<std::uint64_t, ComPtr<ID3D11BlendState>> blend_states_;
  std::unordered_map<std::uint64_t, ComPtr<ID3D11DepthStencilState>>
      depth_states_;
  std::unordered_map<std::uint32_t, RenderTarget> render_targets_;
  std::unordered_map<std::uint32_t, TextureResource> cpu_textures_;
  std::unordered_map<std::uint64_t, TextureResource> uploaded_textures_;
  std::size_t frame_vertex_buffer_capacity_{};
  std::uint32_t back_width_{};
  std::uint32_t back_height_{};
};

std::unique_ptr<Renderer> renderer;

void process_dialog(HWND window) {
  refract::host::DialogModel model;
  {
    std::lock_guard lock(dialog_mutex);
    if (!pending_dialog) return;
    model = std::move(*pending_dialog);
    pending_dialog.reset();
  }
#if defined(REFRACT_HAS_DESKTOP_DIALOGS)
  static_cast<void>(window);
  keyboard_buttons.store(0U, std::memory_order_relaxed);
  keyboard_latched_buttons.store(0U, std::memory_order_relaxed);
  keyboard_analog_directions.store(0U, std::memory_order_relaxed);
  previous_dialog_buttons = 0U;
  dialog_controller_armed = false;
  desktop_dialogs->present(std::move(model));
  InvalidateRect(window_handle, nullptr, FALSE);
#else
  const auto title = utf16(model.title.empty() ? "psprism" : model.title);
  const auto text = utf16(model.message.empty() ? model.detail : model.message);
  UINT flags = MB_ICONINFORMATION;
  if (model.yes_no)
    flags |= MB_YESNO;
  else if (!model.cancel_label.empty())
    flags |= MB_OKCANCEL;
  else
    flags |= MB_OK;
  const auto answer = MessageBoxW(window, text.c_str(), title.c_str(), flags);
  refract::host::DialogResult result;
  result.id = model.id;
  result.selected_item = model.selected_item;
  result.cancelled = answer == IDCANCEL || answer == IDNO;
  result.affirmative = answer == IDOK || answer == IDYES;
  for (const auto& field : model.fields) result.field_text.push_back(field.text);
  {
    std::lock_guard lock(dialog_mutex);
    completed_dialog = std::move(result);
  }
#endif
}

#if defined(REFRACT_HAS_DESKTOP_DIALOGS)
bool embedded_dialog_visible() {
  return desktop_dialogs != nullptr && desktop_dialogs->visible();
}

void handle_dialog_timer(HWND window) {
  refract::desktop::process_desktop_dialog_events();
  if (!embedded_dialog_visible()) {
    previous_dialog_buttons = 0U;
    dialog_controller_armed = false;
    return;
  }
  XINPUT_STATE state{};
  const auto buttons = XInputGetState(0U, &state) == ERROR_SUCCESS
                           ? xinput_buttons(state.Gamepad)
                           : 0U;
  if (!dialog_controller_armed) {
    previous_dialog_buttons = buttons;
    dialog_controller_armed = buttons == 0U;
  } else {
    const auto pressed = buttons & ~previous_dialog_buttons;
    previous_dialog_buttons = buttons;
    if (pressed != 0U) desktop_dialogs->handle_buttons(pressed);
  }
  InvalidateRect(window, nullptr, FALSE);
}

bool handle_dialog_key(WPARAM key, LPARAM attributes) {
  if (!embedded_dialog_visible()) return false;
  if ((attributes & (1LL << 30U)) != 0) return true;
  switch (key) {
    case VK_ESCAPE: desktop_dialogs->cancel(); break;
    case VK_RETURN: desktop_dialogs->accept(); break;
    case VK_BACK: desktop_dialogs->handle_backspace(); break;
    case VK_LEFT: desktop_dialogs->handle_buttons(psp_left); break;
    case VK_RIGHT: desktop_dialogs->handle_buttons(psp_right); break;
    case VK_UP: desktop_dialogs->handle_buttons(psp_up); break;
    case VK_DOWN: desktop_dialogs->handle_buttons(psp_down); break;
    case 'K': desktop_dialogs->handle_buttons(psp_cross); break;
    case 'L': desktop_dialogs->handle_buttons(psp_circle); break;
    case 'J': desktop_dialogs->handle_buttons(psp_square); break;
    default: break;
  }
  InvalidateRect(window_handle, nullptr, FALSE);
  return true;
}
#endif

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam,
                                  LPARAM lparam) {
  switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      BeginPaint(window, &paint);
      if (renderer != nullptr) renderer->draw();
      EndPaint(window, &paint);
      return 0;
    }
    case WM_SIZE:
      if (renderer != nullptr && wparam != SIZE_MINIMIZED) renderer->resize();
      return 0;
    case WM_DPICHANGED: {
      const auto* suggested = reinterpret_cast<const RECT*>(lparam);
      SetWindowPos(window, nullptr, suggested->left, suggested->top,
                   suggested->right - suggested->left,
                   suggested->bottom - suggested->top,
                   SWP_NOACTIVATE | SWP_NOZORDER);
      return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
#if defined(REFRACT_HAS_DESKTOP_DIALOGS)
      if (handle_dialog_key(wparam, lparam)) return 0;
#endif
      if (update_keyboard_key(wparam, true)) return 0;
      break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
#if defined(REFRACT_HAS_DESKTOP_DIALOGS)
      if (embedded_dialog_visible()) return 0;
#endif
      if (update_keyboard_key(wparam, false)) return 0;
      break;
#if defined(REFRACT_HAS_DESKTOP_DIALOGS)
    case WM_CHAR:
      if (embedded_dialog_visible()) {
        if (wparam >= 0x20U && wparam != 0x7fU) {
          const char16_t character = static_cast<char16_t>(wparam);
          desktop_dialogs->handle_text(
              std::u16string_view(&character, 1U));
          InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
      }
      break;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
      if (embedded_dialog_visible()) {
        const auto scale = window_device_pixel_ratio(window);
        const auto x = static_cast<double>(
                           static_cast<short>(LOWORD(lparam))) /
                       scale;
        const auto y = static_cast<double>(
                           static_cast<short>(HIWORD(lparam))) /
                       scale;
        if (message == WM_LBUTTONDOWN) {
          SetCapture(window);
          desktop_dialogs->handle_mouse_press(x, y);
        } else if (message == WM_LBUTTONUP) {
          desktop_dialogs->handle_mouse_release(x, y);
          ReleaseCapture();
        } else {
          desktop_dialogs->handle_mouse_move(x, y);
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
      }
      break;
    case WM_TIMER:
      if (wparam == dialog_timer) {
        handle_dialog_timer(window);
        return 0;
      }
      break;
#endif
    case render_message:
      render_message_pending.store(false, std::memory_order_relaxed);
      InvalidateRect(window, nullptr, FALSE);
      return 0;
    case dialog_message:
      process_dialog(window);
      return 0;
#if defined(REFRACT_HAS_DESKTOP_DIALOGS)
    case dialog_dismiss_message:
      if (desktop_dialogs != nullptr)
        desktop_dialogs->dismiss(static_cast<std::uint64_t>(wparam));
      InvalidateRect(window, nullptr, FALSE);
      return 0;
#endif
    case WM_CLOSE:
      frontend_exit_requested.store(true, std::memory_order_relaxed);
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
#if defined(REFRACT_HAS_DESKTOP_DIALOGS)
      KillTimer(window, dialog_timer);
#endif
      frontend_exit_requested.store(true, std::memory_order_relaxed);
      PostQuitMessage(0);
      return 0;
    default: break;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

std::uint8_t axis_to_byte(int value, int deadzone) {
  value = std::clamp(value, -32768, 32767);
  if (std::abs(value) <= deadzone) return 128U;
  const auto normalized = value < 0
                              ? static_cast<float>(value) / 32768.0F
                              : static_cast<float>(value) / 32767.0F;
  const auto scaled = std::lround((normalized + 1.0F) * 127.5F);
  return static_cast<std::uint8_t>(std::clamp<long>(scaled, 0L, 255L));
}

} // namespace

namespace refract::host {

void set_verbose_logging(bool enabled) {
  verbose_logging.store(enabled, std::memory_order_relaxed);
}

void initialize_frontend() {
  std::call_once(frontend_once, [] {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // Keep the requested client dimensions in logical pixels. On a 200% scaled
    // desktop (including the default Parallels setup), passing 960x544 directly
    // to CreateWindowEx would otherwise produce a window half the macOS size.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#if defined(REFRACT_HAS_DESKTOP_DIALOGS)
    // QApplication must exist before the native HWND so it cannot change the
    // process DPI mode after the window has already been sized.
    desktop_dialogs =
        std::make_unique<refract::desktop::DialogFrontend>();
#endif
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    window_class.lpszClassName = L"PsprismRefractWindow";
    if (RegisterClassExW(&window_class) == 0U &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      std::fprintf(stderr, "psprism: cannot register Win32 window class\n");
      return;
    }
    const auto dpi = GetDpiForSystem();
    RECT rectangle{0L, 0L, MulDiv(960, static_cast<int>(dpi), 96),
                   MulDiv(544, static_cast<int>(dpi), 96)};
    AdjustWindowRectExForDpi(&rectangle, WS_OVERLAPPEDWINDOW, FALSE, 0U, dpi);
    window_handle = CreateWindowExW(
        0U, window_class.lpszClassName, L"psprism",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
        nullptr, nullptr, window_class.hInstance, nullptr);
    if (window_handle == nullptr) {
      std::fprintf(stderr, "psprism: cannot create Win32 window\n");
      return;
    }
    renderer = std::make_unique<Renderer>();
    if (!renderer->initialize(window_handle)) {
      renderer.reset();
      DestroyWindow(window_handle);
      window_handle = nullptr;
      return;
    }
#if defined(REFRACT_HAS_DESKTOP_DIALOGS)
    SetTimer(window_handle, dialog_timer, 16U, nullptr);
#endif
    ShowWindow(window_handle, SW_SHOWDEFAULT);
    UpdateWindow(window_handle);
  });
}

void run_event_loop() {
  initialize_frontend();
  if (window_handle == nullptr) return;
  MSG message{};
  while (!frontend_exit_requested.load(std::memory_order_relaxed)) {
    const auto result = GetMessageW(&message, nullptr, 0U, 0U);
    if (result <= 0) break;
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
}

void request_frontend_exit() {
  if (window_handle != nullptr) PostMessageW(window_handle, WM_CLOSE, 0U, 0U);
}

void present_frame(const std::uint8_t* pixels, std::uint32_t stride,
                   std::uint32_t width, std::uint32_t height,
                   std::uint32_t format, std::uint32_t address) {
  if (pixels == nullptr || width == 0U || height == 0U) return;
  const auto framebuffer_address = normalized_vram_address(address);
  auto converted = std::make_shared<const std::vector<std::uint8_t>>(
      convert_frame(pixels, stride, width, height, format));
  const auto upload_cpu_frame =
      framebuffer_sources.record_cpu_frame(framebuffer_address, *converted);
  {
    std::lock_guard lock(cpu_frame_mutex);
    const auto found = cpu_frames.find(framebuffer_address);
    const auto needs_upload =
        upload_cpu_frame || found == cpu_frames.end() ||
        found->second.width != width || found->second.height != height;
    if (needs_upload)
      cpu_frames[framebuffer_address] = {
          std::move(converted), width, height, ++cpu_frame_generation};
  }
  display_framebuffer_address.store(framebuffer_address,
                                    std::memory_order_relaxed);
  present_ge_frame();
}

void present_ge_frame() {
  keyboard_latched_buttons.store(0U, std::memory_order_relaxed);
  {
    std::lock_guard lock(geometry_mutex);
    if (!pending_geometry_batches.empty())
      presented_geometry_batches = std::move(pending_geometry_batches);
  }
  if (window_handle != nullptr &&
      !render_message_pending.exchange(true, std::memory_order_relaxed))
    PostMessageW(window_handle, render_message, 0U, 0U);
}

void begin_ge_frame() {
  std::lock_guard lock(geometry_mutex);
  building_geometry_batches.clear();
  if (building_geometry_batches.capacity() < 4096U)
    building_geometry_batches.reserve(4096U);
}

void end_ge_frame() {
  std::lock_guard lock(geometry_mutex);
  pending_geometry_batches.reserve(pending_geometry_batches.size() +
                                    building_geometry_batches.size());
  pending_geometry_batches.insert(
      pending_geometry_batches.end(),
      std::make_move_iterator(building_geometry_batches.begin()),
      std::make_move_iterator(building_geometry_batches.end()));
  building_geometry_batches.clear();
}

void submit_ge_primitive(
    std::uint32_t type, std::vector<GeometryVertex> vertices,
    std::shared_ptr<const std::vector<std::uint8_t>> texture,
    std::uint32_t texture_width, std::uint32_t texture_height,
    GeometryState graphics_state) {
  if (type > 4U) return;
  framebuffer_sources.record_ge_write(
      normalized_vram_address(graphics_state.render_target_address));
  std::lock_guard lock(geometry_mutex);
  building_geometry_batches.push_back(
      {type, std::move(vertices), std::move(texture), texture_width,
       texture_height, graphics_state});
}

ControllerState controller_state() {
  ControllerState result;
#if defined(REFRACT_HAS_DESKTOP_DIALOGS)
  if (embedded_dialog_visible()) return result;
#endif
  result.buttons = keyboard_buttons.load(std::memory_order_relaxed) |
                   keyboard_latched_buttons.load(std::memory_order_relaxed);
  const auto directions =
      keyboard_analog_directions.load(std::memory_order_relaxed);
  const auto horizontal = ((directions & analog_right) != 0U ? 1 : 0) -
                          ((directions & analog_left) != 0U ? 1 : 0);
  const auto vertical = ((directions & analog_down) != 0U ? 1 : 0) -
                        ((directions & analog_up) != 0U ? 1 : 0);
  result.analog_x = static_cast<std::uint8_t>(128 + horizontal * 127);
  result.analog_y = static_cast<std::uint8_t>(128 + vertical * 127);
  XINPUT_STATE state{};
  if (XInputGetState(0U, &state) != ERROR_SUCCESS) return result;
  result.buttons |= xinput_buttons(state.Gamepad);
  if (directions == 0U) {
    result.analog_x = axis_to_byte(state.Gamepad.sThumbLX,
                                   XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    result.analog_y = axis_to_byte(-static_cast<int>(state.Gamepad.sThumbLY),
                                   XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
  }
  return result;
}

void present_dialog(DialogModel model) {
  {
    std::lock_guard lock(dialog_mutex);
    pending_dialog = std::move(model);
#if !defined(REFRACT_HAS_DESKTOP_DIALOGS)
    completed_dialog.reset();
#endif
  }
  if (window_handle != nullptr)
    PostMessageW(window_handle, dialog_message, 0U, 0U);
}

std::optional<DialogResult> poll_dialog_result(std::uint64_t id) {
#if defined(REFRACT_HAS_DESKTOP_DIALOGS)
  return desktop_dialogs != nullptr ? desktop_dialogs->take_result(id)
                                    : std::nullopt;
#else
  std::lock_guard lock(dialog_mutex);
  if (!completed_dialog || completed_dialog->id != id) return std::nullopt;
  auto result = std::move(completed_dialog);
  completed_dialog.reset();
  return result;
#endif
}

void dismiss_dialog(std::uint64_t id) {
  {
    std::lock_guard lock(dialog_mutex);
    if (pending_dialog && pending_dialog->id == id) pending_dialog.reset();
#if !defined(REFRACT_HAS_DESKTOP_DIALOGS)
    if (completed_dialog && completed_dialog->id == id)
      completed_dialog.reset();
#endif
  }
#if defined(REFRACT_HAS_DESKTOP_DIALOGS)
  if (window_handle != nullptr)
    PostMessageW(window_handle, dialog_dismiss_message,
                 static_cast<WPARAM>(id), 0U);
#endif
}

bool dialog_visible() {
#if defined(REFRACT_HAS_DESKTOP_DIALOGS)
  if (embedded_dialog_visible()) return true;
  std::lock_guard lock(dialog_mutex);
  return pending_dialog.has_value();
#else
  std::lock_guard lock(dialog_mutex);
  return pending_dialog.has_value();
#endif
}

ge::CacheMetrics ge_cache_metrics() { return ge_cache_counters.snapshot(); }

void reset_ge_cache_metrics() { ge_cache_counters.reset(); }

} // namespace refract::host
