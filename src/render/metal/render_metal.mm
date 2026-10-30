#include "hook/log.h"
#include "render/render_internal.h"
#include "render/render_native.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL3/SDL.h>
#include <cstring>

bool EiMetalHasPendingCommands();

namespace render::detail {
namespace {
id<MTLDevice> g_device;
SDL_MetalView g_view = nullptr;
CAMetalLayer* g_layer;
id<CAMetalDrawable> g_drawable;
id<MTLTexture> Texture(sg_image image) { return (__bridge id<MTLTexture>)sg_mtl_query_image_info(image).tex[0]; }
id<MTLCommandBuffer> Commands() {
    // A blit must follow every recorded sokol pass and precede the next one on the same queue.
    if (EiMetalHasPendingCommands()) CommitGpu();
    return [(__bridge id<MTLCommandQueue>)sg_mtl_command_queue() commandBuffer];
}
}
bool NativeEnvironment(sg_environment* environment) {
    g_device = MTLCreateSystemDefaultDevice();
    environment->metal.device = (__bridge const void*)g_device;
    return g_device != nil;
}
int NativeSampleCount() {
    for (int count = kSmoothSamples; count > 1; count /= 2)
        if ([g_device supportsTextureSampleCount:count]) return count;
    return 1;
}
void DropNativeDevice() {
    g_drawable = nil;
    g_layer = nil;
    if (g_view) SDL_Metal_DestroyView(g_view);
    g_view = nullptr;
    g_device = nil;
}
sg_swapchain NativeDrawable(void* window, bool vsync, int* width, int* height) {
    sg_swapchain swap{};
    auto* sdl = static_cast<SDL_Window*>(window);
    if (!sdl || SDL_GetWindowFlags(sdl) & SDL_WINDOW_MINIMIZED) return swap;
    if (!g_view) {
        g_view = SDL_Metal_CreateView(sdl);
        if (!g_view) return swap;
        g_layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(g_view);
        g_layer.device = g_device;
        g_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        g_layer.framebufferOnly = NO;
        int pointsWidth = 0, pointsHeight = 0, pixelWidth = 0, pixelHeight = 0;
        SDL_GetWindowSize(sdl, &pointsWidth, &pointsHeight);
        SDL_GetWindowSizeInPixels(sdl, &pixelWidth, &pixelHeight);
        ei::Log("render: window %dx%d points, drawable %dx%d pixels (density %.2f)", pointsWidth, pointsHeight,
                pixelWidth, pixelHeight, SDL_GetWindowPixelDensity(sdl));
    }
    SDL_GetWindowSizeInPixels(sdl, width, height);
    if (*width < 1 || *height < 1) return swap;
    g_layer.drawableSize = CGSizeMake(*width, *height);
    g_layer.displaySyncEnabled = vsync;
    g_drawable = [g_layer nextDrawable];
    swap.width = *width;
    swap.height = *height;
    swap.sample_count = 1;
    swap.color_format = SG_PIXELFORMAT_BGRA8;
    swap.depth_format = SG_PIXELFORMAT_NONE;
    swap.metal.current_drawable = (__bridge const void*)g_drawable;
    return swap;
}
void NativeCopyImage(sg_image from, sg_image to, const Rect& region, int x, int y) {
    @autoreleasepool {
        id<MTLCommandBuffer> commands = Commands();
        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        [blit copyFromTexture:Texture(from)
                  sourceSlice:0
                  sourceLevel:0
                 sourceOrigin:MTLOriginMake(region.left, region.top, 0)
                   sourceSize:MTLSizeMake(region.right - region.left, region.bottom - region.top, 1)
                    toTexture:Texture(to)
             destinationSlice:0
             destinationLevel:0
            destinationOrigin:MTLOriginMake(x, y, 0)];
        [blit endEncoding];
        [commands commit];
    }
}
void NativeWriteImage(sg_image image, const Rect& region, const void* bytes, int pitch) {
    @autoreleasepool {
        const std::size_t stride = (static_cast<std::size_t>(pitch) + 255) & ~std::size_t{255};
        const int height = region.bottom - region.top;
        id<MTLBuffer> buffer = [g_device newBufferWithLength:stride * height options:MTLResourceStorageModeShared];
        for (int y = 0; y < height; ++y)
            std::memcpy(static_cast<char*>(buffer.contents) + y * stride, static_cast<const char*>(bytes) + y * pitch,
                        pitch);
        id<MTLCommandBuffer> commands = Commands();
        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        [blit copyFromBuffer:buffer
                   sourceOffset:0
              sourceBytesPerRow:stride
            sourceBytesPerImage:stride * height
                     sourceSize:MTLSizeMake(region.right - region.left, height, 1)
                      toTexture:Texture(image)
               destinationSlice:0
               destinationLevel:0
              destinationOrigin:MTLOriginMake(region.left, region.top, 0)];
        [blit endEncoding];
        [commands commit];
    }
}
void NativeGenerateMipmaps(sg_image image) {
    @autoreleasepool {
        id<MTLCommandBuffer> commands = Commands();
        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        [blit generateMipmapsForTexture:Texture(image)];
        [blit endEncoding];
        [commands commit];
    }
}
bool NativeReadImage(sg_image image, unsigned x, unsigned y, unsigned width, unsigned height,
                     std::vector<std::uint8_t>* bytes, std::size_t* pitch) {
    @autoreleasepool {
        id<MTLTexture> texture = Texture(image);
        if (!texture || !width || !height || x + width > texture.width || y + height > texture.height) return false;
        const auto format = sg_query_image_desc(image).pixel_format;
        const std::size_t pixel = format == SG_PIXELFORMAT_R16UI     ? 2
                                  : format == SG_PIXELFORMAT_R8      ? 1
                                  : format == SG_PIXELFORMAT_RGBA32F ? 16
                                                                     : 4;
        *pitch = (width * pixel + 255) & ~std::size_t{255};
        id<MTLBuffer> buffer = [g_device newBufferWithLength:*pitch * height options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> commands = Commands();
        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        [blit copyFromTexture:texture
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(x, y, 0)
                          sourceSize:MTLSizeMake(width, height, 1)
                            toBuffer:buffer
                   destinationOffset:0
              destinationBytesPerRow:*pitch
            destinationBytesPerImage:*pitch * height];
        [blit endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        if (commands.status == MTLCommandBufferStatusError) return false;
        bytes->resize(*pitch * height);
        std::memcpy(bytes->data(), buffer.contents, bytes->size());
        return true;
    }
}
}   // namespace render::detail
