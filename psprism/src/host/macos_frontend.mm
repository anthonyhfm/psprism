#include "host.hpp"

#if !defined(__APPLE__)
#error "This psprism frontend requires macOS"
#endif

#import <AppKit/AppKit.h>
#import <GameController/GameController.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include <algorithm>
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

struct GeometryBatch {
  MTLPrimitiveType type;
  std::vector<psprism::host::GeometryVertex> vertices;
  std::vector<std::uint8_t> texture;
  std::uint32_t texture_width{};
  std::uint32_t texture_height{};
};

std::mutex geometry_mutex;
std::vector<GeometryBatch> geometry_batches;

@interface PsprismRenderer : NSObject <MTKViewDelegate>
@property(nonatomic, strong) id<MTLDevice> device;
@property(nonatomic, strong) id<MTLCommandQueue> queue;
@property(nonatomic, strong) id<MTLRenderPipelineState> pipeline;
@property(nonatomic, strong) id<MTLRenderPipelineState> geometryPipeline;
@property(nonatomic, strong) id<MTLRenderPipelineState> texturedGeometryPipeline;
@property(nonatomic, strong) id<MTLTexture> texture;
@end

@implementation PsprismRenderer
- (instancetype)initWithView:(MTKView*)view {
  self = [super init];
  if (self == nil) return nil;
  self.device = view.device;
  self.queue = [self.device newCommandQueue];
  NSString* source = @R"METAL(
    #include <metal_stdlib>
    using namespace metal;
    struct VertexOut { float4 position [[position]]; float2 uv; };
    vertex VertexOut psprism_vertex(uint id [[vertex_id]]) {
      constexpr float2 positions[] = {{-1.0,-1.0},{3.0,-1.0},{-1.0,3.0}};
      constexpr float2 texcoords[] = {{0.0,1.0},{2.0,1.0},{0.0,-1.0}};
      return {float4(positions[id], 0.0, 1.0), texcoords[id]};
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
    vertex GeometryOut psprism_geometry_vertex(
        uint id [[vertex_id]], device const GeometryVertex* vertices [[buffer(0)]]) {
      return {vertices[id].position, vertices[id].color, vertices[id].texture};
    }
    fragment float4 psprism_geometry_fragment(GeometryOut in [[stage_in]]) {
      return in.color;
    }
    fragment float4 psprism_textured_geometry_fragment(
        GeometryOut in [[stage_in]], texture2d<float> image [[texture(0)]]) {
      constexpr sampler linear_sampler(filter::linear, address::clamp_to_edge);
      return image.sample(linear_sampler, in.texture) * in.color;
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
  MTLRenderPipelineDescriptor* descriptor =
      [[MTLRenderPipelineDescriptor alloc] init];
  descriptor.vertexFunction = [library newFunctionWithName:@"psprism_vertex"];
  descriptor.fragmentFunction =
      [library newFunctionWithName:@"psprism_fragment"];
  descriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat;
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
  return self;
}

- (void)drawInMTKView:(MTKView*)view {
  MTLRenderPassDescriptor* pass = view.currentRenderPassDescriptor;
  id<CAMetalDrawable> drawable = view.currentDrawable;
  if (pass == nil || drawable == nil || self.pipeline == nil) return;
  id<MTLCommandBuffer> commands = [self.queue commandBuffer];
  id<MTLRenderCommandEncoder> encoder =
      [commands renderCommandEncoderWithDescriptor:pass];
  [encoder setRenderPipelineState:self.pipeline];
  if (self.texture != nil) [encoder setFragmentTexture:self.texture atIndex:0];
  [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
  if (self.geometryPipeline != nil) {
    std::lock_guard lock(geometry_mutex);
    for (const auto& batch : geometry_batches) {
      if (batch.vertices.empty()) continue;
      if (batch.texture.empty()) {
        [encoder setRenderPipelineState:self.geometryPipeline];
      } else {
        [encoder setRenderPipelineState:self.texturedGeometryPipeline];
        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                         width:batch.texture_width
                                        height:batch.texture_height
                                     mipmapped:NO];
        id<MTLTexture> texture =
            [self.device newTextureWithDescriptor:descriptor];
        [texture replaceRegion:MTLRegionMake2D(0, 0, batch.texture_width,
                                               batch.texture_height)
                  mipmapLevel:0
                    withBytes:batch.texture.data()
                  bytesPerRow:batch.texture_width * 4U];
        [encoder setFragmentTexture:texture atIndex:0];
      }
      [encoder setVertexBytes:batch.vertices.data()
                       length:batch.vertices.size() * sizeof(psprism::host::GeometryVertex)
                      atIndex:0];
      [encoder drawPrimitives:batch.type
                  vertexStart:0
                  vertexCount:batch.vertices.size()];
    }
  }
  [encoder endEncoding];
  [commands presentDrawable:drawable];
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
                  std::fprintf(stderr, "[psprism:controller] connected: %s\n",
                               active_controller.vendorName.UTF8String);
                }];
    if (GCController.controllers.count != 0)
      active_controller = GCController.controllers.firstObject;
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
                   std::uint32_t format) {
  if (pixels == nullptr || width == 0 || height == 0) return;
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
}

void begin_ge_frame() {
  std::lock_guard lock(geometry_mutex);
  geometry_batches.clear();
}

void submit_ge_primitive(std::uint32_t type,
                         std::vector<GeometryVertex> vertices,
                         std::vector<std::uint8_t> texture,
                         std::uint32_t texture_width,
                         std::uint32_t texture_height) {
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
    geometry_batches.push_back({metal_type, std::move(vertices),
                                std::move(texture), texture_width,
                                texture_height});
  }
  dispatch_async(dispatch_get_main_queue(), ^{ [metal_view setNeedsDisplay:YES]; });
}

ControllerState controller_state() {
  ControllerState result;
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
  result.analog_x = axis_to_byte(pad.leftThumbstick.xAxis.value);
  result.analog_y = axis_to_byte(-pad.leftThumbstick.yAxis.value);
  return result;
}

} // namespace psprism::host
