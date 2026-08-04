#include "host.hpp"

#if !defined(__APPLE__)
#error "This psprism frontend requires macOS"
#endif

#import <AppKit/AppKit.h>
#import <GameController/GameController.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <vector>

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

std::uint32_t normalized_vram_address(std::uint32_t address) {
  if (address < 0x00200000U ||
      (address & 0x0fe00000U) == 0x04000000U)
    return 0x04000000U | (address & 0x001fffffU);
  return address;
}

struct GeometryBatch {
  MTLPrimitiveType type;
  std::vector<psprism::host::GeometryVertex> vertices;
  std::shared_ptr<const std::vector<std::uint8_t>> texture;
  std::uint32_t texture_width{};
  std::uint32_t texture_height{};
  psprism::host::GeometryState state;
};

struct FragmentState {
  std::uint32_t color_test{};
  std::uint32_t color_function{1};
  std::uint32_t color_reference{};
  std::uint32_t color_mask{0x00ffffff};
  std::uint32_t alpha_test{};
  std::uint32_t alpha_function{1};
  std::uint32_t alpha_reference{};
  std::uint32_t alpha_mask{0xff};
  std::uint32_t texture_function{};
  std::uint32_t texture_alpha_used{1};
  std::uint32_t texture_environment_color{};
};

MTLBlendFactor psp_blend_factor(std::uint32_t factor,
                                std::uint32_t fixed_color, bool source) {
  if (factor >= 10U) {
    if ((fixed_color & 0x00ffffffU) == 0U)
      return MTLBlendFactorZero;
    if ((fixed_color & 0x00ffffffU) == 0x00ffffffU)
      return MTLBlendFactorOne;
    return MTLBlendFactorBlendColor;
  }
  constexpr MTLBlendFactor source_factors[] = {
      MTLBlendFactorDestinationColor,
      MTLBlendFactorOneMinusDestinationColor,
      MTLBlendFactorSourceAlpha,
      MTLBlendFactorOneMinusSourceAlpha,
      MTLBlendFactorDestinationAlpha,
      MTLBlendFactorOneMinusDestinationAlpha,
      MTLBlendFactorSourceAlpha,
      MTLBlendFactorOneMinusSourceAlpha,
      MTLBlendFactorDestinationAlpha,
      MTLBlendFactorOneMinusDestinationAlpha,
  };
  constexpr MTLBlendFactor destination_factors[] = {
      MTLBlendFactorSourceColor,
      MTLBlendFactorOneMinusSourceColor,
      MTLBlendFactorSourceAlpha,
      MTLBlendFactorOneMinusSourceAlpha,
      MTLBlendFactorDestinationAlpha,
      MTLBlendFactorOneMinusDestinationAlpha,
      MTLBlendFactorSourceAlpha,
      MTLBlendFactorOneMinusSourceAlpha,
      MTLBlendFactorDestinationAlpha,
      MTLBlendFactorOneMinusDestinationAlpha,
  };
  return source ? source_factors[factor] : destination_factors[factor];
}

MTLBlendOperation psp_blend_operation(std::uint32_t equation) {
  switch (equation) {
    case 1U: return MTLBlendOperationSubtract;
    case 2U: return MTLBlendOperationReverseSubtract;
    case 3U: return MTLBlendOperationMin;
    case 4U: return MTLBlendOperationMax;
    default: return MTLBlendOperationAdd;
  }
}

std::mutex geometry_mutex;
std::vector<GeometryBatch> building_geometry_batches;
std::vector<GeometryBatch> pending_geometry_batches;
std::vector<GeometryBatch> presented_geometry_batches;
std::atomic_uint32_t display_framebuffer_address{0x04000000U};
std::atomic_uint32_t keyboard_buttons{};
std::atomic_uint32_t keyboard_latched_buttons{};
std::atomic_uint32_t keyboard_analog_directions{};
std::atomic_bool verbose_logging{};
id keyboard_event_monitor;

constexpr std::uint32_t analog_left = 1U << 0U;
constexpr std::uint32_t analog_right = 1U << 1U;
constexpr std::uint32_t analog_up = 1U << 2U;
constexpr std::uint32_t analog_down = 1U << 3U;

void update_keyboard_mask(std::atomic_uint32_t& state, std::uint32_t mask,
                          bool pressed) {
  if (pressed)
    state.fetch_or(mask, std::memory_order_relaxed);
  else
    state.fetch_and(~mask, std::memory_order_relaxed);
}

void update_keyboard_button(std::uint32_t mask, bool pressed) {
  update_keyboard_mask(keyboard_buttons, mask, pressed);
  if (pressed)
    keyboard_latched_buttons.fetch_or(mask, std::memory_order_relaxed);
}

bool update_keyboard_key(unsigned short key_code, bool pressed) {
  switch (key_code) {
    case 126: update_keyboard_button(psp_up, pressed); break;
    case 124: update_keyboard_button(psp_right, pressed); break;
    case 125: update_keyboard_button(psp_down, pressed); break;
    case 123: update_keyboard_button(psp_left, pressed); break;
    case 12: update_keyboard_button(psp_l, pressed); break;
    case 14: update_keyboard_button(psp_r, pressed); break;
    case 34: update_keyboard_button(psp_triangle, pressed); break;
    case 37: update_keyboard_button(psp_circle, pressed); break;
    case 40: update_keyboard_button(psp_cross, pressed); break;
    case 38: update_keyboard_button(psp_square, pressed); break;
    case 36: update_keyboard_button(psp_start, pressed); break;
    case 60: update_keyboard_button(psp_select, pressed); break;
    case 0:
      update_keyboard_mask(keyboard_analog_directions, analog_left, pressed);
      break;
    case 2:
      update_keyboard_mask(keyboard_analog_directions, analog_right, pressed);
      break;
    case 13:
      update_keyboard_mask(keyboard_analog_directions, analog_up, pressed);
      break;
    case 1:
      update_keyboard_mask(keyboard_analog_directions, analog_down, pressed);
      break;
    default: return false;
  }
  return true;
}

@interface PsprismRenderer : NSObject <MTKViewDelegate>
@property(nonatomic, strong) id<MTLDevice> device;
@property(nonatomic, strong) id<MTLCommandQueue> queue;
@property(nonatomic, strong) id<MTLRenderPipelineState> pipeline;
@property(nonatomic, strong) id<MTLRenderPipelineState> geometryPipeline;
@property(nonatomic, strong) id<MTLRenderPipelineState> texturedGeometryPipeline;
@property(nonatomic, strong) id<MTLRenderPipelineState> blendedGeometryPipeline;
@property(nonatomic, strong) id<MTLRenderPipelineState> blendedTexturedGeometryPipeline;
@property(nonatomic, strong) id<MTLLibrary> shaderLibrary;
@property(nonatomic, strong) NSMutableDictionary<NSNumber*, id<MTLRenderPipelineState>>* blendPipelines;
@property(nonatomic, strong) id<MTLTexture> texture;
@property(nonatomic, strong) NSArray<id<MTLDepthStencilState>>* depthStates;
@property(nonatomic, strong) NSArray<id<MTLSamplerState>>* samplerStates;
@property(nonatomic, strong) NSMutableDictionary<NSNumber*, id<MTLTexture>>* renderTargets;
@property(nonatomic, strong) NSMutableDictionary<NSNumber*, id<MTLTexture>>* depthTargets;
@end

@implementation PsprismRenderer
- (instancetype)initWithView:(MTKView*)view {
  self = [super init];
  if (self == nil) return nil;
  self.device = view.device;
  self.queue = [self.device newCommandQueue];
  self.renderTargets = [NSMutableDictionary dictionary];
  self.depthTargets = [NSMutableDictionary dictionary];
  NSString* source = @R"METAL(
    #include <metal_stdlib>
    using namespace metal;
    struct VertexOut { float4 position [[position]]; float2 uv; };
    vertex VertexOut psprism_vertex(
        uint id [[vertex_id]], constant float2& texture_scale [[buffer(0)]]) {
      constexpr float2 positions[] = {{-1.0,-1.0},{3.0,-1.0},{-1.0,3.0}};
      constexpr float2 texcoords[] = {{0.0,1.0},{2.0,1.0},{0.0,-1.0}};
      return {float4(positions[id], 0.0, 1.0),
              texcoords[id] * texture_scale};
    }
    fragment float4 psprism_fragment(VertexOut in [[stage_in]],
                                     texture2d<float> image [[texture(0)]]) {
      constexpr sampler nearest(filter::nearest, address::clamp_to_edge);
      return image.sample(nearest, in.uv);
    }
    struct GeometryVertex {
      packed_float4 position;
      packed_float4 color;
      packed_float2 texture;
    };
    struct GeometryOut {
      float4 position [[position]];
      float4 color;
      float2 texture;
    };
    struct FragmentState {
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
      uint texture_environment_color;
    };
    bool psprism_compare(uint value, uint reference, uint function) {
      switch (function) {
        case 0: return false;
        case 1: return true;
        case 2: return value == reference;
        case 3: return value != reference;
        case 4: return value < reference;
        case 5: return value <= reference;
        case 6: return value > reference;
        default: return value >= reference;
      }
    }
    float4 psprism_fragment_tests(float4 color, constant FragmentState& state) {
      uint3 rgb = uint3(round(saturate(color.rgb) * 255.0));
      uint packed_rgb = rgb.r | (rgb.g << 8) | (rgb.b << 16);
      uint masked_rgb = packed_rgb & state.color_mask & 0x00ffffff;
      uint masked_reference =
          state.color_reference & state.color_mask & 0x00ffffff;
      bool color_passes = state.color_function == 1 ||
                          (state.color_function == 2 &&
                           masked_rgb == masked_reference) ||
                          (state.color_function == 3 &&
                           masked_rgb != masked_reference);
      if (state.color_test != 0 && !color_passes)
        discard_fragment();
      uint alpha = uint(round(saturate(color.a) * 255.0)) & state.alpha_mask;
      uint reference = state.alpha_reference & state.alpha_mask;
      if (state.alpha_test != 0 &&
          !psprism_compare(alpha, reference, state.alpha_function))
        discard_fragment();
      return color;
    }
    float4 psprism_texture_combine(float4 texture_color, float4 primary,
                                   constant FragmentState& state) {
      bool use_alpha = state.texture_alpha_used != 0;
      float alpha = use_alpha ? texture_color.a * primary.a : primary.a;
      switch (state.texture_function) {
        case 1:
          return float4(mix(primary.rgb, texture_color.rgb,
                            use_alpha ? texture_color.a : 1.0), primary.a);
        case 2: {
          float3 environment = float3(
              float(state.texture_environment_color & 0xff) / 255.0,
              float((state.texture_environment_color >> 8) & 0xff) / 255.0,
              float((state.texture_environment_color >> 16) & 0xff) / 255.0);
          return float4(mix(primary.rgb, environment, texture_color.rgb), alpha);
        }
        case 3:
          return float4(texture_color.rgb,
                        use_alpha ? texture_color.a : primary.a);
        case 4:
          return float4(texture_color.rgb + primary.rgb, alpha);
        default:
          return float4(texture_color.rgb * primary.rgb, alpha);
      }
    }
    vertex GeometryOut psprism_geometry_vertex(
        uint id [[vertex_id]], device const GeometryVertex* vertices [[buffer(0)]]) {
      return {vertices[id].position, vertices[id].color, vertices[id].texture};
    }
    fragment float4 psprism_geometry_fragment(
        GeometryOut in [[stage_in]], constant FragmentState& state [[buffer(1)]]) {
      return psprism_fragment_tests(in.color, state);
    }
    fragment float4 psprism_textured_geometry_fragment(
        GeometryOut in [[stage_in]], texture2d<float> image [[texture(0)]],
        sampler texture_sampler [[sampler(0)]],
        constant FragmentState& state [[buffer(1)]]) {
      float4 color = psprism_texture_combine(
          image.sample(texture_sampler, in.texture), in.color, state);
      return psprism_fragment_tests(color, state);
    }
  )METAL";
  NSError* error = nil;
  id<MTLLibrary> library = [self.device newLibraryWithSource:source
                                                    options:nil
                                                      error:&error];
  if (library == nil) {
    NSLog(@"psprism: Metal shader error: %@", error);
    return nil;
  }
  self.shaderLibrary = library;
  self.blendPipelines = [NSMutableDictionary dictionary];
  MTLRenderPipelineDescriptor* descriptor =
      [[MTLRenderPipelineDescriptor alloc] init];
  descriptor.vertexFunction = [library newFunctionWithName:@"psprism_vertex"];
  descriptor.fragmentFunction =
      [library newFunctionWithName:@"psprism_fragment"];
  descriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat;
  descriptor.depthAttachmentPixelFormat = view.depthStencilPixelFormat;
  self.pipeline = [self.device newRenderPipelineStateWithDescriptor:descriptor
                                                               error:&error];
  if (self.pipeline == nil) NSLog(@"psprism: Metal pipeline error: %@", error);
  descriptor.vertexFunction =
      [library newFunctionWithName:@"psprism_geometry_vertex"];
  descriptor.fragmentFunction =
      [library newFunctionWithName:@"psprism_geometry_fragment"];
  self.geometryPipeline =
      [self.device newRenderPipelineStateWithDescriptor:descriptor error:&error];
  if (self.geometryPipeline == nil)
    NSLog(@"psprism: Metal geometry pipeline error: %@", error);
  descriptor.fragmentFunction =
      [library newFunctionWithName:@"psprism_textured_geometry_fragment"];
  self.texturedGeometryPipeline =
      [self.device newRenderPipelineStateWithDescriptor:descriptor error:&error];
  if (self.texturedGeometryPipeline == nil)
    NSLog(@"psprism: Metal textured pipeline error: %@", error);
  descriptor.colorAttachments[0].blendingEnabled = YES;
  descriptor.colorAttachments[0].sourceRGBBlendFactor =
      MTLBlendFactorSourceAlpha;
  descriptor.colorAttachments[0].destinationRGBBlendFactor =
      MTLBlendFactorOneMinusSourceAlpha;
  descriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
  descriptor.colorAttachments[0].sourceAlphaBlendFactor =
      MTLBlendFactorSourceAlpha;
  descriptor.colorAttachments[0].destinationAlphaBlendFactor =
      MTLBlendFactorOneMinusSourceAlpha;
  descriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
  descriptor.fragmentFunction =
      [library newFunctionWithName:@"psprism_geometry_fragment"];
  self.blendedGeometryPipeline =
      [self.device newRenderPipelineStateWithDescriptor:descriptor error:&error];
  if (self.blendedGeometryPipeline == nil)
    NSLog(@"psprism: Metal blended geometry pipeline error: %@", error);
  descriptor.fragmentFunction =
      [library newFunctionWithName:@"psprism_textured_geometry_fragment"];
  self.blendedTexturedGeometryPipeline =
      [self.device newRenderPipelineStateWithDescriptor:descriptor error:&error];
  if (self.blendedTexturedGeometryPipeline == nil)
    NSLog(@"psprism: Metal blended textured pipeline error: %@", error);

  NSMutableArray<id<MTLDepthStencilState>>* depth_states =
      [NSMutableArray arrayWithCapacity:32];
  constexpr MTLCompareFunction compare_functions[] = {
      MTLCompareFunctionNever,        MTLCompareFunctionAlways,
      MTLCompareFunctionEqual,        MTLCompareFunctionNotEqual,
      MTLCompareFunctionLess,         MTLCompareFunctionLessEqual,
      MTLCompareFunctionGreater,      MTLCompareFunctionGreaterEqual,
  };
  for (NSUInteger write = 0; write < 2; ++write) {
    for (NSUInteger enabled = 0; enabled < 2; ++enabled) {
      for (NSUInteger function = 0; function < 8; ++function) {
        MTLDepthStencilDescriptor* depth_descriptor =
            [[MTLDepthStencilDescriptor alloc] init];
        depth_descriptor.depthCompareFunction =
            enabled != 0 ? compare_functions[function]
                         : MTLCompareFunctionAlways;
        depth_descriptor.depthWriteEnabled = write != 0;
        [depth_states addObject:[self.device
                                    newDepthStencilStateWithDescriptor:
                                        depth_descriptor]];
      }
    }
  }
  self.depthStates = depth_states;
  NSMutableArray<id<MTLSamplerState>>* sampler_states =
      [NSMutableArray arrayWithCapacity:8];
  for (NSUInteger linear = 0; linear < 2; ++linear) {
    for (NSUInteger clamp_t = 0; clamp_t < 2; ++clamp_t) {
      for (NSUInteger clamp_s = 0; clamp_s < 2; ++clamp_s) {
        MTLSamplerDescriptor* sampler_descriptor =
            [[MTLSamplerDescriptor alloc] init];
        sampler_descriptor.minFilter =
            linear != 0 ? MTLSamplerMinMagFilterLinear
                        : MTLSamplerMinMagFilterNearest;
        sampler_descriptor.magFilter = sampler_descriptor.minFilter;
        sampler_descriptor.sAddressMode =
            clamp_s != 0 ? MTLSamplerAddressModeClampToEdge
                         : MTLSamplerAddressModeRepeat;
        sampler_descriptor.tAddressMode =
            clamp_t != 0 ? MTLSamplerAddressModeClampToEdge
                         : MTLSamplerAddressModeRepeat;
        [sampler_states
            addObject:[self.device newSamplerStateWithDescriptor:
                                       sampler_descriptor]];
      }
    }
  }
  self.samplerStates = sampler_states;
  return self;
}

- (id<MTLRenderPipelineState>)blendPipelineForView:(MTKView*)view
                                           textured:(BOOL)textured
                                              state:(const psprism::host::GeometryState&)state {
  const auto source_fixed_class = state.blend_source >= 10U
                                      ? (state.blend_fix_a == 0U ? 1U
                                         : state.blend_fix_a == 0x00ffffffU ? 2U
                                                                           : 3U)
                                      : 0U;
  const auto destination_fixed_class = state.blend_destination >= 10U
                                           ? (state.blend_fix_b == 0U ? 1U
                                              : state.blend_fix_b == 0x00ffffffU ? 2U
                                                                                : 3U)
                                           : 0U;
  const std::uint32_t key = (textured ? 1U : 0U) |
                            ((state.blend_source & 0xfU) << 1U) |
                            ((state.blend_destination & 0xfU) << 5U) |
                            ((state.blend_equation & 7U) << 9U) |
                            (source_fixed_class << 12U) |
                            (destination_fixed_class << 14U);
  if (id<MTLRenderPipelineState> cached = self.blendPipelines[@(key)])
    return cached;
  MTLRenderPipelineDescriptor* descriptor =
      [[MTLRenderPipelineDescriptor alloc] init];
  descriptor.vertexFunction =
      [self.shaderLibrary newFunctionWithName:@"psprism_geometry_vertex"];
  descriptor.fragmentFunction = [self.shaderLibrary
      newFunctionWithName:textured ? @"psprism_textured_geometry_fragment"
                                   : @"psprism_geometry_fragment"];
  descriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat;
  descriptor.depthAttachmentPixelFormat = view.depthStencilPixelFormat;
  auto* attachment = descriptor.colorAttachments[0];
  attachment.blendingEnabled = YES;
  attachment.sourceRGBBlendFactor = psp_blend_factor(
      std::min(state.blend_source, 10U), state.blend_fix_a, true);
  attachment.destinationRGBBlendFactor = psp_blend_factor(
      std::min(state.blend_destination, 10U), state.blend_fix_b, false);
  attachment.rgbBlendOperation =
      psp_blend_operation(state.blend_equation);
  attachment.sourceAlphaBlendFactor = MTLBlendFactorOne;
  attachment.destinationAlphaBlendFactor = MTLBlendFactorZero;
  attachment.alphaBlendOperation = MTLBlendOperationAdd;
  NSError* error = nil;
  id<MTLRenderPipelineState> pipeline =
      [self.device newRenderPipelineStateWithDescriptor:descriptor error:&error];
  if (pipeline == nil) {
    NSLog(@"psprism: Metal PSP blend pipeline error: %@", error);
    return textured ? self.blendedTexturedGeometryPipeline
                    : self.blendedGeometryPipeline;
  }
  self.blendPipelines[@(key)] = pipeline;
  return pipeline;
}

- (void)drawInMTKView:(MTKView*)view {
  MTLRenderPassDescriptor* display_pass = view.currentRenderPassDescriptor;
  id<CAMetalDrawable> drawable = view.currentDrawable;
  if (display_pass == nil || drawable == nil || self.pipeline == nil) return;
  id<MTLCommandBuffer> commands = [self.queue commandBuffer];
  NSMutableDictionary<NSValue*, id<MTLTexture>>* uploaded_textures =
      [NSMutableDictionary dictionary];
  if (self.geometryPipeline != nil) {
    std::lock_guard lock(geometry_mutex);
    id<MTLRenderCommandEncoder> encoder = nil;
    std::uint32_t active_target = UINT32_MAX;
    for (const auto& batch : presented_geometry_batches) {
      if (batch.vertices.empty()) continue;
      const auto target_address =
          normalized_vram_address(batch.state.render_target_address);
      NSNumber* target_key = @(target_address);
      id<MTLTexture> target =
          [self.renderTargets objectForKey:target_key];
      id<MTLTexture> depth = [self.depthTargets objectForKey:target_key];
      bool created_target = false;
      if (target == nil || target.width < batch.state.render_target_width ||
          target.height < batch.state.render_target_height) {
        const auto target_width =
            std::max<NSUInteger>(target.width,
                                 batch.state.render_target_width);
        const auto target_height =
            std::max<NSUInteger>(target.height,
                                 batch.state.render_target_height);
        MTLTextureDescriptor* target_descriptor = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:view.colorPixelFormat
                                         width:target_width
                                        height:target_height
                                     mipmapped:NO];
        target_descriptor.usage =
            MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        target = [self.device newTextureWithDescriptor:target_descriptor];
        [self.renderTargets setObject:target forKey:target_key];
        MTLTextureDescriptor* depth_descriptor = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:view.depthStencilPixelFormat
                                         width:target_width
                                        height:target_height
                                     mipmapped:NO];
        depth_descriptor.usage = MTLTextureUsageRenderTarget;
        depth = [self.device newTextureWithDescriptor:depth_descriptor];
        [self.depthTargets setObject:depth forKey:target_key];
        created_target = true;
      }
      if (encoder == nil || active_target != target_address) {
        if (encoder != nil) [encoder endEncoding];
        MTLRenderPassDescriptor* target_pass =
            [MTLRenderPassDescriptor renderPassDescriptor];
        target_pass.colorAttachments[0].texture = target;
        target_pass.colorAttachments[0].loadAction =
            created_target ? MTLLoadActionClear : MTLLoadActionLoad;
        target_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        target_pass.colorAttachments[0].clearColor =
            MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
        target_pass.depthAttachment.texture = depth;
        target_pass.depthAttachment.loadAction =
            created_target ? MTLLoadActionClear : MTLLoadActionLoad;
        target_pass.depthAttachment.storeAction = MTLStoreActionStore;
        target_pass.depthAttachment.clearDepth = 1.0;
        encoder = [commands renderCommandEncoderWithDescriptor:target_pass];
        const MTLViewport target_viewport{
            0.0, 0.0, static_cast<double>(target.width),
            static_cast<double>(target.height), 0.0, 1.0};
        [encoder setViewport:target_viewport];
        active_target = target_address;
      }
      [encoder setCullMode:batch.state.cull_face ? MTLCullModeBack
                                                 : MTLCullModeNone];
      [encoder setFrontFacingWinding:batch.state.front_face_clockwise
                                         ? MTLWindingClockwise
                                         : MTLWindingCounterClockwise];
      const auto depth_state =
          (batch.state.depth_write ? 16U : 0U) +
          (batch.state.depth_test ? 8U : 0U) +
          std::min(batch.state.depth_function, 7U);
      [encoder setDepthStencilState:self.depthStates[depth_state]];
      const FragmentState fragment_state{
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
          batch.state.texture_environment_color};
      [encoder setFragmentBytes:&fragment_state
                         length:sizeof(fragment_state)
                        atIndex:1];
      id<MTLTexture> sampled_texture = nil;
      const auto texture_address =
          normalized_vram_address(batch.state.texture_address);
      if (batch.state.texture_address != 0U)
        sampled_texture = [self.renderTargets objectForKey:@(texture_address)];
      if (sampled_texture == nil && batch.texture != nullptr &&
          !batch.texture->empty()) {
        NSValue* texture_key = [NSValue valueWithPointer:batch.texture.get()];
        sampled_texture = [uploaded_textures objectForKey:texture_key];
        if (sampled_texture == nil) {
          MTLTextureDescriptor* descriptor = [MTLTextureDescriptor
              texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                           width:batch.texture_width
                                          height:batch.texture_height
                                       mipmapped:NO];
          sampled_texture = [self.device newTextureWithDescriptor:descriptor];
          [sampled_texture
              replaceRegion:MTLRegionMake2D(0, 0, batch.texture_width,
                                            batch.texture_height)
               mipmapLevel:0
                 withBytes:batch.texture->data()
               bytesPerRow:batch.texture_width * 4U];
          [uploaded_textures setObject:sampled_texture forKey:texture_key];
        }
      }
      if (sampled_texture == nil) {
        [encoder setRenderPipelineState:
                     batch.state.alpha_blend
                         ? [self blendPipelineForView:view
                                            textured:NO
                                               state:batch.state]
                         : self.geometryPipeline];
      } else {
        [encoder setRenderPipelineState:
                     batch.state.alpha_blend
                         ? [self blendPipelineForView:view
                                            textured:YES
                                               state:batch.state]
                         : self.texturedGeometryPipeline];
        const auto sampler_state =
            (batch.state.texture_linear_filter ? 4U : 0U) +
            (batch.state.texture_clamp_t ? 2U : 0U) +
            (batch.state.texture_clamp_s ? 1U : 0U);
        [encoder setFragmentSamplerState:self.samplerStates[sampler_state]
                                 atIndex:0];
        [encoder setFragmentTexture:sampled_texture atIndex:0];
      }
      if (batch.state.alpha_blend &&
          (batch.state.blend_source >= 10U ||
           batch.state.blend_destination >= 10U)) {
        const auto fixed = batch.state.blend_source >= 10U
                               ? batch.state.blend_fix_a
                               : batch.state.blend_fix_b;
        [encoder setBlendColorRed:static_cast<double>(fixed & 0xffU) / 255.0
                            green:static_cast<double>((fixed >> 8U) & 0xffU) / 255.0
                             blue:static_cast<double>((fixed >> 16U) & 0xffU) / 255.0
                            alpha:1.0];
      }
      [encoder setVertexBytes:batch.vertices.data()
                       length:batch.vertices.size() * sizeof(psprism::host::GeometryVertex)
                      atIndex:0];
      [encoder drawPrimitives:batch.type
                  vertexStart:0
                  vertexCount:batch.vertices.size()];
    }
    if (encoder != nil) [encoder endEncoding];
    // A PSP frame may be assembled from several GE lists.  Consume every
    // completed list once, in submission order, after encoding it into the
    // persistent framebuffer targets.
    presented_geometry_batches.clear();
  }

  id<MTLTexture> display_texture = [self.renderTargets
      objectForKey:@(normalized_vram_address(
                       display_framebuffer_address.load()))];
  if (display_texture == nil) display_texture = self.texture;
  id<MTLRenderCommandEncoder> display_encoder =
      [commands renderCommandEncoderWithDescriptor:display_pass];
  [display_encoder setRenderPipelineState:self.pipeline];
  [display_encoder setDepthStencilState:self.depthStates[0]];
  if (display_texture != nil) {
    const float texture_scale[2]{
        std::min(1.0F, 480.0F / static_cast<float>(display_texture.width)),
        std::min(1.0F, 272.0F / static_cast<float>(display_texture.height))};
    [display_encoder setVertexBytes:texture_scale
                            length:sizeof(texture_scale)
                           atIndex:0];
    [display_encoder setFragmentTexture:display_texture atIndex:0];
    [display_encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:3];
  }
  [display_encoder endEncoding];
  [commands presentDrawable:drawable];
  [commands addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
    if (buffer.error != nil)
      NSLog(@"psprism: Metal command buffer error: %@", buffer.error);
  }];
  [commands commit];
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
  static_cast<void>(view);
  static_cast<void>(size);
}
@end

@interface PsprismApplicationDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@end

@implementation PsprismApplicationDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
  static_cast<void>(sender);
  return YES;
}

- (void)windowWillClose:(NSNotification*)notification {
  static_cast<void>(notification);
  [NSApp terminate:nil];
}
@end

NSWindow* window;
MTKView* metal_view;
PsprismRenderer* renderer;
PsprismApplicationDelegate* application_delegate;
GCController* active_controller;
std::once_flag frontend_once;

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
  std::vector<std::uint8_t> output(static_cast<std::size_t>(width) * height * 4U);
  const auto bytes_per_pixel = format == 3U ? 4U : 2U;
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      const auto source_offset =
          (static_cast<std::size_t>(y) * stride + x) * bytes_per_pixel;
      const auto destination = (static_cast<std::size_t>(y) * width + x) * 4U;
      std::uint8_t red{};
      std::uint8_t green{};
      std::uint8_t blue{};
      std::uint8_t alpha{255};
      if (format == 3U) {
        red = source[source_offset];
        green = source[source_offset + 1U];
        blue = source[source_offset + 2U];
        alpha = source[source_offset + 3U];
      } else {
        const auto packed = static_cast<std::uint16_t>(source[source_offset]) |
                            static_cast<std::uint16_t>(source[source_offset + 1U]) << 8U;
        if (format == 0U) {
          red = expand5(packed & 31U);
          green = static_cast<std::uint8_t>(((packed >> 5U) & 63U) * 255U / 63U);
          blue = expand5((packed >> 11U) & 31U);
        } else if (format == 1U) {
          red = expand5(packed & 31U);
          green = expand5((packed >> 5U) & 31U);
          blue = expand5((packed >> 10U) & 31U);
          alpha = (packed & 0x8000U) != 0 ? 255 : 0;
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

std::uint8_t axis_to_byte(float value) {
  const auto scaled = std::lround((std::clamp(value, -1.0F, 1.0F) + 1.0F) * 127.5F);
  return static_cast<std::uint8_t>(std::clamp<long>(scaled, 0, 255));
}

namespace psprism::host {

void set_verbose_logging(bool enabled) {
  verbose_logging.store(enabled, std::memory_order_relaxed);
}

void initialize_frontend() {
  std::call_once(frontend_once, [] {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    application_delegate = [[PsprismApplicationDelegate alloc] init];
    NSApp.delegate = application_delegate;
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (device == nil) {
      std::fprintf(stderr, "psprism: Metal is unavailable\n");
      return;
    }
    const NSRect frame = NSMakeRect(0, 0, 960, 544);
    window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable |
                            NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    window.title = @"psprism";
    window.delegate = application_delegate;
    metal_view = [[MTKView alloc] initWithFrame:frame device:device];
    metal_view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    metal_view.depthStencilPixelFormat = MTLPixelFormatDepth32Float;
    metal_view.clearDepth = 1.0;
    metal_view.clearColor = MTLClearColorMake(0.02, 0.02, 0.025, 1.0);
    metal_view.paused = YES;
    metal_view.enableSetNeedsDisplay = YES;
    renderer = [[PsprismRenderer alloc] initWithView:metal_view];
    metal_view.delegate = renderer;
    window.contentView = metal_view;
    [window center];
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    [[NSNotificationCenter defaultCenter]
        addObserverForName:GCControllerDidConnectNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification* notification) {
                  active_controller = notification.object;
                  if (verbose_logging.load(std::memory_order_relaxed))
                    std::fprintf(
                        stderr, "[psprism:controller] connected: %s\n",
                        active_controller.vendorName.UTF8String);
                }];
    if (GCController.controllers.count != 0)
      active_controller = GCController.controllers.firstObject;
    keyboard_event_monitor = [NSEvent
        addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown |
                                              NSEventMaskKeyUp
                                    handler:^NSEvent*(NSEvent* event) {
                                      const auto pressed =
                                          event.type == NSEventTypeKeyDown;
                                      const auto handled = update_keyboard_key(
                                          event.keyCode, pressed);
                                      if (handled && pressed &&
                                          !event.isARepeat &&
                                          verbose_logging.load(
                                              std::memory_order_relaxed))
                                        std::fprintf(
                                            stderr,
                                            "[psprism:keyboard] key=%hu\n",
                                            event.keyCode);
                                      return handled ? nil : event;
                                    }];
  });
}

void run_event_loop() {
  initialize_frontend();
  [NSApp run];
}

void request_frontend_exit() {
  dispatch_async(dispatch_get_main_queue(), ^{ [NSApp terminate:nil]; });
}

void present_frame(const std::uint8_t* pixels, std::uint32_t stride,
                   std::uint32_t width, std::uint32_t height,
                   std::uint32_t format, std::uint32_t address) {
  if (pixels == nullptr || width == 0 || height == 0) return;
  display_framebuffer_address.store(normalized_vram_address(address));
  auto converted = std::make_shared<std::vector<std::uint8_t>>(
      convert_frame(pixels, stride, width, height, format));
  dispatch_async(dispatch_get_main_queue(), ^{
    if (renderer == nil) return;
    MTLTextureDescriptor* descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    renderer.texture = [renderer.device newTextureWithDescriptor:descriptor];
    const MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    [renderer.texture replaceRegion:region mipmapLevel:0
                          withBytes:converted->data()
                        bytesPerRow:static_cast<NSUInteger>(width) * 4U];
    [metal_view setNeedsDisplay:YES];
  });
  present_ge_frame();
}

void present_ge_frame() {
  bool has_geometry = false;
  {
    std::lock_guard lock(geometry_mutex);
    has_geometry = !pending_geometry_batches.empty();
    presented_geometry_batches.reserve(presented_geometry_batches.size() +
                                        pending_geometry_batches.size());
    presented_geometry_batches.insert(
        presented_geometry_batches.end(),
        std::make_move_iterator(pending_geometry_batches.begin()),
        std::make_move_iterator(pending_geometry_batches.end()));
    pending_geometry_batches.clear();
  }
  if (has_geometry)
    dispatch_async(dispatch_get_main_queue(), ^{ [metal_view setNeedsDisplay:YES]; });
}

void begin_ge_frame() {
  std::lock_guard lock(geometry_mutex);
  building_geometry_batches.clear();
  if (building_geometry_batches.capacity() < 4096U)
    building_geometry_batches.reserve(4096U);
}

void end_ge_frame() {
  {
    std::lock_guard lock(geometry_mutex);
    pending_geometry_batches.reserve(pending_geometry_batches.size() +
                                      building_geometry_batches.size());
    pending_geometry_batches.insert(
        pending_geometry_batches.end(),
        std::make_move_iterator(building_geometry_batches.begin()),
        std::make_move_iterator(building_geometry_batches.end()));
    building_geometry_batches.clear();
  }
}

void submit_ge_primitive(std::uint32_t type,
                         std::vector<GeometryVertex> vertices,
                         std::shared_ptr<const std::vector<std::uint8_t>>
                             texture,
                         std::uint32_t texture_width,
                         std::uint32_t texture_height,
                         GeometryState graphics_state) {
  MTLPrimitiveType metal_type;
  switch (type) {
    case 0:
      metal_type = MTLPrimitiveTypePoint;
      break;
    case 1:
      metal_type = MTLPrimitiveTypeLine;
      break;
    case 2:
      metal_type = MTLPrimitiveTypeLineStrip;
      break;
    case 3:
      metal_type = MTLPrimitiveTypeTriangle;
      break;
    case 4:
      metal_type = MTLPrimitiveTypeTriangleStrip;
      break;
    default:
      return;
  }
  {
    std::lock_guard lock(geometry_mutex);
    building_geometry_batches.push_back(
        {metal_type, std::move(vertices), std::move(texture), texture_width,
         texture_height, graphics_state});
  }
}

ControllerState controller_state() {
  ControllerState result;
  result.buttons = keyboard_buttons.load(std::memory_order_relaxed) |
                   keyboard_latched_buttons.exchange(0,
                                                     std::memory_order_relaxed);
  const auto directions =
      keyboard_analog_directions.load(std::memory_order_relaxed);
  const auto horizontal = ((directions & analog_right) != 0 ? 1 : 0) -
                          ((directions & analog_left) != 0 ? 1 : 0);
  const auto vertical = ((directions & analog_down) != 0 ? 1 : 0) -
                        ((directions & analog_up) != 0 ? 1 : 0);
  result.analog_x = static_cast<std::uint8_t>(128 + horizontal * 127);
  result.analog_y = static_cast<std::uint8_t>(128 + vertical * 127);
  GCExtendedGamepad* pad = active_controller.extendedGamepad;
  if (pad == nil) return result;
  if (pad.dpad.up.isPressed) result.buttons |= psp_up;
  if (pad.dpad.right.isPressed) result.buttons |= psp_right;
  if (pad.dpad.down.isPressed) result.buttons |= psp_down;
  if (pad.dpad.left.isPressed) result.buttons |= psp_left;
  if (pad.leftShoulder.isPressed) result.buttons |= psp_l;
  if (pad.rightShoulder.isPressed) result.buttons |= psp_r;
  if (pad.buttonA.isPressed) result.buttons |= psp_cross;
  if (pad.buttonB.isPressed) result.buttons |= psp_circle;
  if (pad.buttonX.isPressed) result.buttons |= psp_square;
  if (pad.buttonY.isPressed) result.buttons |= psp_triangle;
  if (pad.buttonMenu.isPressed) result.buttons |= psp_start;
  if ([pad respondsToSelector:@selector(buttonOptions)] && pad.buttonOptions.isPressed)
    result.buttons |= psp_select;
  if (directions == 0) {
    result.analog_x = axis_to_byte(pad.leftThumbstick.xAxis.value);
    result.analog_y = axis_to_byte(-pad.leftThumbstick.yAxis.value);
  }
  return result;
}

} // namespace psprism::host
